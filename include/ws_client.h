#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <curl/curl.h>
#include "constants.h"
#include "curl_runtime.h"
#include "ws_transport.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <poll.h>
#endif

/**
 * @file ws_client.h
 * @brief libcurl WebSocket implementation of WsTransport.
 *
 * One easy handle owns the upgraded connection for its lifetime. There is no
 * reader thread and no auto-reconnect: the caller drives `receive` (usually
 * via `WfmSocket::poll`) and reconnects when a dead socket throws.
 *
 * Global curl state is initialised once through `wftEnsureCurlGlobalInit`.
 * This client does not take the HTTP 350 ms rate-limit lock.
 */

namespace ws_detail
{

/// Outcome of feeding one `curl_ws_recv` chunk into the assembler.
enum class FeedResult
{
    NeedMore,          ///< Text message still assembling.
    FinishedText,      ///< Complete text message ready in the buffer.
    Ignore,            ///< Control or discarded frame; keep waiting.
    ConnectionClosed,  ///< Peer sent a close frame.
    HardFailure,       ///< Oversized buffer or other unrecoverable state.
};

/**
 * @brief Assembles fragmented WebSocket text messages from `curl_ws_recv`.
 *
 * Text payloads are buffered across intervening control frames. A message is
 * finished only when `bytesleft == 0` and `CURLWS_CONT` is clear. Binary
 * frames are discarded. Close fails the connection. The buffer is capped at
 * 1 MiB.
 */
class FrameAssembler
{
public:
    static constexpr std::size_t kMaxBufferBytes = 1024 * 1024;

    /**
     * @brief Consume one receive result.
     * @param flags `curl_ws_frame::flags` bitset.
     * @param data Payload bytes for this chunk (may be empty).
     * @param len Byte count of `data`.
     * @param bytesleft Remaining bytes in the current frame after this chunk.
     */
    [[nodiscard]] FeedResult feed(unsigned int flags, const char *data, std::size_t len,
                                   curl_off_t bytesleft)
    {
        if ((flags & CURLWS_CLOSE) != 0)
        {
            clear();
            return FeedResult::ConnectionClosed;
        }
        if ((flags & (CURLWS_PING | CURLWS_PONG)) != 0)
        {
            return FeedResult::Ignore;
        }
        if ((flags & CURLWS_BINARY) != 0)
        {
            return FeedResult::Ignore;
        }

        if (len > 0)
        {
            if (data == nullptr)
            {
                clear();
                return FeedResult::HardFailure;
            }
            if (buffer_.size() + len > kMaxBufferBytes)
            {
                clear();
                return FeedResult::HardFailure;
            }
            buffer_.append(data, len);
        }

        if (bytesleft == 0 && (flags & CURLWS_CONT) == 0)
        {
            return FeedResult::FinishedText;
        }
        return FeedResult::NeedMore;
    }

    /// Move out the finished text buffer and reset.
    [[nodiscard]] std::string takeText()
    {
        std::string out = std::move(buffer_);
        buffer_.clear();
        return out;
    }

    void clear() { buffer_.clear(); }

private:
    std::string buffer_;
};

struct CurlHandleDeleter
{
    void operator()(CURL *handle) const noexcept { curl_easy_cleanup(handle); }
};

using CurlHandle = std::unique_ptr<CURL, CurlHandleDeleter>;

/// `curl_slist` owner that can grow, matching `http_detail::CurlSlist`.
class CurlSlist
{
public:
    CurlSlist() = default;
    CurlSlist(const CurlSlist &) = delete;
    CurlSlist &operator=(const CurlSlist &) = delete;
    ~CurlSlist() { curl_slist_free_all(list_); }

    void append(const std::string &line)
    {
        curl_slist *next = curl_slist_append(list_, line.c_str());
        if (next == nullptr)
        {
            throw std::runtime_error("Failed to build curl WebSocket headers");
        }
        list_ = next;
    }

    [[nodiscard]] curl_slist *get() const noexcept { return list_; }

private:
    curl_slist *list_ = nullptr;
};

/**
 * @brief Wait until `sock` is readable or writable, or the deadline passes.
 * @return True when the socket is ready; false on timeout or poll error.
 */
inline bool waitSocket(curl_socket_t sock, bool forWrite, int timeoutMs)
{
    if (sock == CURL_SOCKET_BAD || timeoutMs < 0)
    {
        return false;
    }

#if defined(_WIN32)
    WSAPOLLFD pfd{};
    pfd.fd = sock;
    pfd.events = forWrite ? POLLWRNORM : POLLRDNORM;
    const int rc = WSAPoll(&pfd, 1, timeoutMs);
    return rc > 0;
#else
    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = forWrite ? POLLOUT : POLLIN;
    const int rc = ::poll(&pfd, 1, timeoutMs);
    return rc > 0;
#endif
}

inline int remainingMs(std::chrono::steady_clock::time_point deadline)
{
    const auto left = deadline - std::chrono::steady_clock::now();
    if (left <= std::chrono::steady_clock::duration::zero())
    {
        return 0;
    }
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
}

} // namespace ws_detail

/**
 * @brief Production `WsTransport` over libcurl WebSockets.
 */
class WsClient : public WsTransport
{
public:
    WsClient() { wftEnsureCurlGlobalInit(); }

    void connect(const std::string &url, const std::string &subprotocol) override
    {
        wftEnsureCurlGlobalInit();
        if (handle_)
        {
            throw std::runtime_error("WsClient: already connected; close() first");
        }

        ws_detail::CurlHandle handle(curl_easy_init());
        if (!handle)
        {
            throw std::runtime_error("WsClient: failed to initialize curl");
        }

        // Keep headers alive for the duration of curl_easy_perform.
        auto headers = std::make_unique<ws_detail::CurlSlist>();
        headers->append(std::format("Sec-WebSocket-Protocol: {}", subprotocol));
        headers->append(std::format("User-Agent: {}", WFT_USER_AGENT));

        curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers->get());
        curl_easy_setopt(handle.get(), CURLOPT_CONNECT_ONLY, 2L);
        curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        // Do not set CURLOPT_TIMEOUT: a healthy long-lived subscription must
        // not be killed by an idle transfer limit.

        const CURLcode result = curl_easy_perform(handle.get());
        long responseCode = 0;
        curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &responseCode);

        if (result != CURLE_OK || responseCode != 101)
        {
            const std::string detail = result != CURLE_OK
                                           ? curl_easy_strerror(result)
                                           : std::format("unexpected HTTP status {}", responseCode);
            throw std::runtime_error(std::format("WsClient: handshake failed: {}", detail));
        }

        handle_ = std::move(handle);
        headers_ = std::move(headers);
        assembler_.clear();
    }

    void close() override
    {
        if (!handle_)
        {
            return;
        }
        size_t sent = 0;
        (void)curl_ws_send(handle_.get(), "", 0, &sent, 0, CURLWS_CLOSE);
        destroyHandle();
    }

    [[nodiscard]] bool isConnected() const override { return static_cast<bool>(handle_); }

    void sendText(const std::string &message) override
    {
        requireConnected("sendText");

        std::size_t offset = 0;
        const std::size_t total = message.size();
        for (;;)
        {
            size_t sent = 0;
            const CURLcode rc = curl_ws_send(handle_.get(), message.data() + offset,
                                             total - offset, &sent, 0, CURLWS_TEXT);
            if (rc == CURLE_OK)
            {
                offset += sent;
                if (offset >= total)
                {
                    return;
                }
                continue;
            }
            if (rc == CURLE_AGAIN)
            {
                curl_socket_t sock = CURL_SOCKET_BAD;
                curl_easy_getinfo(handle_.get(), CURLINFO_ACTIVESOCKET, &sock);
                (void)ws_detail::waitSocket(sock, true, 1000);
                continue;
            }

            destroyHandle();
            throw std::runtime_error(
                std::format("WsClient: send failed: {}", curl_easy_strerror(rc)));
        }
    }

    bool receive(std::string &out, int timeout_ms) override
    {
        requireConnected("receive");

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0
                                                                                         : timeout_ms);

        char buffer[16384];
        while (true)
        {
            const int leftMs = ws_detail::remainingMs(deadline);
            size_t received = 0;
            const curl_ws_frame *meta = nullptr;
            const CURLcode rc =
                curl_ws_recv(handle_.get(), buffer, sizeof(buffer), &received, &meta);

            if (rc == CURLE_AGAIN)
            {
                if (leftMs <= 0)
                {
                    return false;
                }
                curl_socket_t sock = CURL_SOCKET_BAD;
                curl_easy_getinfo(handle_.get(), CURLINFO_ACTIVESOCKET, &sock);
                if (!ws_detail::waitSocket(sock, false, leftMs))
                {
                    return false;
                }
                continue;
            }

            if (rc == CURLE_GOT_NOTHING || rc != CURLE_OK || meta == nullptr)
            {
                destroyHandle();
                throw std::runtime_error(std::format(
                    "WsClient: receive failed: {}",
                    rc == CURLE_OK ? "missing frame metadata" : curl_easy_strerror(rc)));
            }

            const ws_detail::FeedResult fed =
                assembler_.feed(static_cast<unsigned int>(meta->flags), buffer, received,
                                meta->bytesleft);

            switch (fed)
            {
            case ws_detail::FeedResult::FinishedText:
                out = assembler_.takeText();
                return true;
            case ws_detail::FeedResult::NeedMore:
            case ws_detail::FeedResult::Ignore:
                if (ws_detail::remainingMs(deadline) <= 0)
                {
                    return false;
                }
                continue;
            case ws_detail::FeedResult::ConnectionClosed:
                destroyHandle();
                throw std::runtime_error("WsClient: connection closed");
            case ws_detail::FeedResult::HardFailure:
                destroyHandle();
                throw std::runtime_error("WsClient: receive failed: frame assembler error");
            }
        }
    }

private:
    ws_detail::CurlHandle handle_;
    std::unique_ptr<ws_detail::CurlSlist> headers_;
    ws_detail::FrameAssembler assembler_;

    void destroyHandle()
    {
        handle_.reset();
        headers_.reset();
        assembler_.clear();
    }

    void requireConnected(std::string_view op) const
    {
        if (!handle_)
        {
            throw std::runtime_error(std::format("WsClient: {} on closed connection", op));
        }
    }
};

#endif // WS_CLIENT_H
