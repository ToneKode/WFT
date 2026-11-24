#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "http_transport.h"

using json = nlohmann::json;

/**
 * @file http_client.h
 * @brief libcurl implementation of HttpTransport.
 *
 * Two things here are not incidental:
 *
 * - libcurl's global state is initialised once per process. Doing it in the
 *   constructor and tearing it down in the destructor breaks as soon as a
 *   second client exists, because the first destructor invalidates handles the
 *   second one still uses. A function-local static keeps init/cleanup tied to
 *   program lifetime instead.
 * - Rate limiting is shared across clients rather than per-object. The API
 *   limit is a per-client figure and a process can hold several clients
 *   (a trader plus a price watcher, say), so the pacing has to be global to be
 *   worth anything.
 */
class HttpClient : public HttpTransport
{
public:
    /**
     * @brief Construct a client.
     * @param minDelay Minimum gap between requests; defaults to the documented
     *        3 requests per second.
     */
    explicit HttpClient(std::chrono::milliseconds minDelay = std::chrono::milliseconds(350))
        : minDelay_(minDelay)
    {
        ensureGlobalInit();
    }

    std::string Get(const std::string &url, const Headers &headers) override
    {
        return perform("GET", url, json(), headers, nullptr);
    }

private:
    std::chrono::milliseconds minDelay_;

    /// libcurl global init, run once per process.
    static void ensureGlobalInit()
    {
        static const bool initialised = []()
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            return true;
        }();
        (void)initialised;
    }

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
            std::lock_guard<std::mutex> lock(gate);
            const auto now = std::chrono::steady_clock::now();
            slot = (std::max)(now, nextAllowed);
            nextAllowed = slot + minDelay_;
        }

        const auto wait = slot - std::chrono::steady_clock::now();
        if (wait > std::chrono::milliseconds::zero())
        {
            std::this_thread::sleep_for(wait);
        }
    }

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *output)
    {
        output->append(static_cast<char *>(contents), size * nmemb);
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

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        const auto first = value.find_first_not_of(" \t");
        if (first == std::string::npos)
        {
            value.clear();
        }
        else
        {
            value.erase(0, first);
            const auto last = value.find_last_not_of(" \t\r\n");
            value.erase(last + 1);
        }

        (*headers)[key] = value;
        return total;
    }

    /// One curl exchange, with headers captured when requested.
    void execute(const std::string &method, const std::string &url, const json &payload,
                 const Headers &headers, Headers *response_headers, CURL *&handle,
                 std::string &response_body, long &http_code)
    {
        awaitSlot();

        handle = curl_easy_init();
        if (!handle)
        {
            throw std::runtime_error("Failed to initialize curl");
        }

        curl_slist *curl_headers = nullptr;
        for (const auto &header : headers)
        {
            curl_headers = curl_slist_append(curl_headers, (header.first + ": " + header.second).c_str());
        }

        const std::string body = payload.is_null() ? std::string() : payload.dump();

        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, curl_headers);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");

        if (response_headers != nullptr)
        {
            curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, headerCallback);
            curl_easy_setopt(handle, CURLOPT_HEADERDATA, response_headers);
        }

        if (method != "GET")
        {
            curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
        }
        if (!body.empty())
        {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(body.size()));
        }

        const CURLcode result = curl_easy_perform(handle);
        curl_slist_free_all(curl_headers);

        if (result != CURLE_OK)
        {
            curl_easy_cleanup(handle);
            handle = nullptr;
            throw std::runtime_error(std::string("Curl error: ") + curl_easy_strerror(result));
        }

        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(handle);
        handle = nullptr;
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
    std::string perform(const std::string &method, const std::string &url,
                        const json &payload, const Headers &headers,
                        Headers *response_headers)
    {
        constexpr int max_attempts = 4;

        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            CURL *handle = nullptr;
            std::string body;
            long status = 0;
            execute(method, url, payload, headers, response_headers, handle, body, status);

            if (status >= 200 && status < 300)
            {
                return body;
            }

            const bool throttled = (status == 429 || status == 509 || status >= 500);
            if (!throttled || attempt + 1 == max_attempts)
            {
                throw HttpStatusError(status, body);
            }

            // Exponential backoff with jitter, so parallel clients do not
            // resume in lockstep and re-trigger the limit.
            const int base_ms = 500 * (1 << attempt);
            std::random_device rd;
            std::uniform_int_distribution<int> jitter(0, base_ms / 2);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(base_ms + jitter(rd)));
        }

        throw std::runtime_error("Request failed after retries: " + method + " " + url);
    }
};

#endif // HTTP_CLIENT_H
