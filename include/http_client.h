#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <mutex>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "curl_runtime.h"
#include "http_transport.h"

using json = nlohmann::json;

/**
 * @file http_client.h
 * @brief libcurl implementation of HttpTransport.
 *
 * Three things here are not incidental:
 *
 * - libcurl's global state is initialised once per process via
 *   `wftEnsureCurlGlobalInit` (see `curl_runtime.h`). Doing init in one
 *   constructor and cleanup in its destructor breaks as soon as a second
 *   client exists.
 * - Ease handles and header lists are owned by RAII wrappers. Every early exit
 *   (failed perform, thrown status) used to be a manual cleanup site.
 * - Rate limiting is shared across clients rather than per-object. The API
 *   limit is a per-client figure and a process can hold several clients
 *   (a trader plus a price watcher, say), so the pacing has to be global to be
 *   worth anything.
 */
namespace http_detail
{

/// `curl_easy_cleanup` as a deleter, so a handle cannot outlive its scope.
struct CurlHandleDeleter
{
    void operator()(CURL *handle) const noexcept { curl_easy_cleanup(handle); }
};

using CurlHandle = std::unique_ptr<CURL, CurlHandleDeleter>;

/**
 * @brief Owner for a `curl_slist` that can grow.
 *
 * `curl_slist_append` returns the new list head rather than mutating in place,
 * so this cannot be a plain `unique_ptr`: it holds the current head and frees
 * whatever it holds when the exchange ends, including on a throw.
 */
class CurlSlist
{
public:
    CurlSlist() = default;
    CurlSlist(const CurlSlist &) = delete;
    CurlSlist &operator=(const CurlSlist &) = delete;
    ~CurlSlist() { curl_slist_free_all(list_); }

    /// Append one `Name: value` line; throws when curl cannot allocate it.
    void append(const std::string &line)
    {
        curl_slist *next = curl_slist_append(list_, line.c_str());
        if (next == nullptr)
        {
            throw std::runtime_error("Failed to build curl request headers");
        }
        list_ = next;
    }

    [[nodiscard]] curl_slist *get() const noexcept { return list_; }

private:
    curl_slist *list_ = nullptr;
};

} // namespace http_detail

class HttpClient : public HttpTransport
{
public:
    using Headers = HttpTransport::Headers;

    /**
     * @brief Construct a client.
     * @param minDelay Minimum gap between requests; defaults to the documented
     *        3 requests per second.
     */
    explicit HttpClient(std::chrono::milliseconds minDelay = std::chrono::milliseconds(350))
        : minDelay_(minDelay)
    {
        wftEnsureCurlGlobalInit();
    }

    std::string Get(const std::string &url, const Headers &headers) override
    {
        return perform("GET", url, json(), headers, nullptr);
    }

private:
    std::chrono::milliseconds minDelay_;

    /**
     * @brief Wait until the shared rate-limit slot is free, then claim it.
     *
     * Reserving the slot under the lock and sleeping outside it keeps threads
     * from serialising on the sleep while still spacing requests apart.
     */
    void awaitSlot()
    {
        static std::mutex gate;
        static std::chrono::steady_clock::time_point nextAllowed =
            std::chrono::steady_clock::now();

        std::chrono::steady_clock::time_point slot;
        {
            std::lock_guard lock(gate);
            const auto now = std::chrono::steady_clock::now();
            slot = std::ranges::max(now, nextAllowed);
            nextAllowed = slot + minDelay_;
        }

        // A slot already in the past returns immediately.
        std::this_thread::sleep_until(slot);
    }

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *output)
    {
        output->append(static_cast<const char *>(contents), size * nmemb);
        return size * nmemb;
    }

    static size_t headerCallback(char *buffer, size_t size, size_t nitems, Headers *headers)
    {
        const size_t total = size * nitems;
        std::string line(buffer, total);

        const auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            return total;
        }

        std::string_view value = std::string_view(line).substr(colon + 1);
        if (const auto first = value.find_first_not_of(" \t"); first == std::string_view::npos)
        {
            value = {};
        }
        else
        {
            value.remove_prefix(first);
            if (const auto last = value.find_last_not_of(" \t\r\n");
                last != std::string_view::npos)
            {
                value = value.substr(0, last + 1);
            }
        }

        (*headers)[line.substr(0, colon)] = value;
        return total;
    }

    /// One curl exchange, with headers captured when requested.
    void execute(const std::string &method, const std::string &url, const json &payload,
                 const Headers &headers, Headers *response_headers, std::string &response_body,
                 long &http_code)
    {
        awaitSlot();

        const http_detail::CurlHandle handle(curl_easy_init());
        if (!handle)
        {
            throw std::runtime_error("Failed to initialize curl");
        }

        http_detail::CurlSlist curl_headers;
        for (const auto &[name, value] : headers)
        {
            curl_headers.append(std::format("{}: {}", name, value));
        }

        const std::string body = payload.is_null() ? std::string() : payload.dump();

        curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, curl_headers.get());
        curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");

        if (response_headers != nullptr)
        {
            curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, headerCallback);
            curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, response_headers);
        }

        if (method != "GET")
        {
            curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
        }
        if (!body.empty())
        {
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }

        if (const CURLcode result = curl_easy_perform(handle.get()); result != CURLE_OK)
        {
            throw std::runtime_error(std::format("Curl error: {}", curl_easy_strerror(result)));
        }

        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &http_code);
    }

    /**
     * @brief Perform a request, retrying the statuses that mean "slow down".
     *
     * Cloudflare answers `429` when the rate limit is exceeded and `509` when
     * one address opens too many connections. Both are transient, so a short
     * jittered backoff succeeds where an immediate throw would not. Other
     * non-2xx statuses are surfaced as HttpStatusError so the API layer can
     * read the v2 error envelope out of the body.
     */
    std::string perform(const std::string &method, const std::string &url, const json &payload,
                        const Headers &headers, Headers *response_headers)
    {
        constexpr int max_attempts = 4;

        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            std::string body;
            long status = 0;
            execute(method, url, payload, headers, response_headers, body, status);

            if (status >= 200 && status < 300)
            {
                return body;
            }

            const bool throttled = (status == 429 || status == 509 || status >= 500);
            if (!throttled || attempt + 1 == max_attempts)
            {
                throw HttpStatusError(status, std::move(body));
            }

            // Exponential backoff with jitter, so parallel clients do not
            // resume in lockstep and re-trigger the limit.
            const int base_ms = 500 * (1 << attempt);
            std::random_device rd;
            std::uniform_int_distribution<int> jitter(0, base_ms / 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(base_ms + jitter(rd)));
        }

        throw std::runtime_error(std::format("Request failed after retries: {} {}", method, url));
    }
};

#endif // HTTP_CLIENT_H
