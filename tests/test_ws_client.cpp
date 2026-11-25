#include <doctest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "wfm_socket.h"
#include "ws_client.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#endif

// Offline coverage for the frame assembler and for WsClient behaviour that
// does not dial Warframe.Market. Protocol dispatch stays in test_wfm_socket.cpp.

namespace
{

using ws_detail::FeedResult;
using ws_detail::FrameAssembler;

// Compact SHA-1 for Sec-WebSocket-Accept. Kept local so the test does not add
// an OpenSSL link dependency beyond what CURL::libcurl already pulls in.
class Sha1
{
public:
    Sha1() { reset(); }

    void update(const unsigned char *data, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            block_[blockLen_++] = data[i];
            if (blockLen_ == 64)
            {
                transform();
                bitCount_ += 512;
                blockLen_ = 0;
            }
        }
    }

    void final(unsigned char out[20])
    {
        bitCount_ += static_cast<std::uint64_t>(blockLen_) * 8;
        block_[blockLen_++] = 0x80;
        if (blockLen_ > 56)
        {
            while (blockLen_ < 64)
            {
                block_[blockLen_++] = 0;
            }
            transform();
            blockLen_ = 0;
        }
        while (blockLen_ < 56)
        {
            block_[blockLen_++] = 0;
        }
        for (int i = 7; i >= 0; --i)
        {
            block_[blockLen_++] = static_cast<unsigned char>((bitCount_ >> (i * 8)) & 0xff);
        }
        transform();
        for (int i = 0; i < 5; ++i)
        {
            out[i * 4] = static_cast<unsigned char>((state_[i] >> 24) & 0xff);
            out[i * 4 + 1] = static_cast<unsigned char>((state_[i] >> 16) & 0xff);
            out[i * 4 + 2] = static_cast<unsigned char>((state_[i] >> 8) & 0xff);
            out[i * 4 + 3] = static_cast<unsigned char>(state_[i] & 0xff);
        }
    }

private:
    void reset()
    {
        state_[0] = 0x67452301u;
        state_[1] = 0xEFCDAB89u;
        state_[2] = 0x98BADCFEu;
        state_[3] = 0x10325476u;
        state_[4] = 0xC3D2E1F0u;
        bitCount_ = 0;
        blockLen_ = 0;
    }

    static std::uint32_t rol(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void transform()
    {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(block_[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block_[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block_[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block_[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
        {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::uint32_t state_[5]{};
    std::uint64_t bitCount_ = 0;
    std::size_t blockLen_ = 0;
    unsigned char block_[64]{};
};

std::string base64Encode(const unsigned char *data, std::size_t len)
{
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3)
    {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16) |
                               ((i + 1 < len ? static_cast<unsigned int>(data[i + 1]) : 0u) << 8) |
                               (i + 2 < len ? static_cast<unsigned int>(data[i + 2]) : 0u);
        out.push_back(kTable[(n >> 18) & 63]);
        out.push_back(kTable[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? kTable[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? kTable[n & 63] : '=');
    }
    return out;
}

std::string secWebSocketAccept(const std::string &key)
{
    const std::string material = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha1 sha;
    sha.update(reinterpret_cast<const unsigned char *>(material.data()), material.size());
    unsigned char digest[20];
    sha.final(digest);
    return base64Encode(digest, 20);
}

void closeSocket(socket_t sock)
{
#if defined(_WIN32)
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
    }
#else
    if (sock != invalid_socket)
    {
        ::close(sock);
    }
#endif
}

/**
 * @brief Local listener that completes one WebSocket HTTP upgrade then idles.
 *
 * Enough for `WsClient::connect` to leave a live handle without dialling the
 * public API, so a second `connect` can exercise the already-connected guard.
 */
class LocalWsUpgradeServer
{
public:
    LocalWsUpgradeServer()
    {
#if defined(_WIN32)
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
        REQUIRE(listen_ != INVALID_SOCKET);
#else
        REQUIRE(listen_ != invalid_socket);
#endif

        int yes = 1;
        setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes),
                   sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(listen_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_, 1) == 0);

        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listen_, reinterpret_cast<sockaddr *>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);

        thread_ = std::thread([this] { serveOnce(); });
    }

    ~LocalWsUpgradeServer()
    {
        stop_ = true;
        closeSocket(listen_);
        closeSocket(client_);
        if (thread_.joinable())
        {
            thread_.join();
        }
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    [[nodiscard]] std::string url() const
    {
        return "ws://127.0.0.1:" + std::to_string(port_);
    }

private:
    void serveOnce()
    {
        client_ = ::accept(listen_, nullptr, nullptr);
#if defined(_WIN32)
        if (client_ == INVALID_SOCKET)
        {
            return;
        }
#else
        if (client_ == invalid_socket)
        {
            return;
        }
#endif

        std::string request;
        char buf[1024];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            const auto n = ::recv(client_, buf, sizeof(buf), 0);
            if (n <= 0)
            {
                return;
            }
            request.append(buf, static_cast<std::size_t>(n));
        }

        const std::string keyHeader = "Sec-WebSocket-Key:";
        const auto keyPos = request.find(keyHeader);
        if (keyPos == std::string::npos)
        {
            return;
        }
        auto keyStart = keyPos + keyHeader.size();
        while (keyStart < request.size() && (request[keyStart] == ' ' || request[keyStart] == '\t'))
        {
            ++keyStart;
        }
        const auto keyEnd = request.find("\r\n", keyStart);
        if (keyEnd == std::string::npos)
        {
            return;
        }
        const std::string key = request.substr(keyStart, keyEnd - keyStart);
        const std::string accept = secWebSocketAccept(key);

        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Protocol: wfm\r\n"
            "Sec-WebSocket-Accept: " +
            accept + "\r\n\r\n";
        (void)::send(client_, response.data(), static_cast<int>(response.size()), 0);

        while (!stop_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    socket_t listen_{
#if defined(_WIN32)
        INVALID_SOCKET
#else
        invalid_socket
#endif
    };
    socket_t client_{
#if defined(_WIN32)
        INVALID_SOCKET
#else
        invalid_socket
#endif
    };
    std::uint16_t port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace

TEST_CASE("assembler finishes a single text frame")
{
    FrameAssembler assembler;
    const std::string payload = R"({"route":"x"})";

    const FeedResult result =
        assembler.feed(CURLWS_TEXT, payload.data(), payload.size(), 0);

    CHECK(result == FeedResult::FinishedText);
    CHECK(assembler.takeText() == payload);
}

TEST_CASE("assembler joins two text fragments")
{
    FrameAssembler assembler;

    CHECK(assembler.feed(CURLWS_TEXT | CURLWS_CONT, "hello", 5, 0) == FeedResult::NeedMore);
    CHECK(assembler.feed(0, " world", 6, 0) == FeedResult::FinishedText);
    CHECK(assembler.takeText() == "hello world");
}

TEST_CASE("assembler keeps fragments across an intervening ping")
{
    FrameAssembler assembler;

    CHECK(assembler.feed(CURLWS_TEXT | CURLWS_CONT, "ab", 2, 0) == FeedResult::NeedMore);
    CHECK(assembler.feed(CURLWS_PING, "ping", 4, 0) == FeedResult::Ignore);
    CHECK(assembler.feed(0, "cd", 2, 0) == FeedResult::FinishedText);
    CHECK(assembler.takeText() == "abcd");
}

TEST_CASE("assembler discards binary frames")
{
    FrameAssembler assembler;

    CHECK(assembler.feed(CURLWS_BINARY, "bin", 3, 0) == FeedResult::Ignore);
    CHECK(assembler.feed(CURLWS_TEXT, "ok", 2, 0) == FeedResult::FinishedText);
    CHECK(assembler.takeText() == "ok");
}

TEST_CASE("assembler fails the connection on a close frame")
{
    FrameAssembler assembler;

    CHECK(assembler.feed(CURLWS_CLOSE, "", 0, 0) == FeedResult::ConnectionClosed);
}

TEST_CASE("assembler rejects an oversized buffer")
{
    FrameAssembler assembler;
    const std::string chunk(FrameAssembler::kMaxBufferBytes / 2 + 1, 'x');

    CHECK(assembler.feed(CURLWS_TEXT | CURLWS_CONT, chunk.data(), chunk.size(), 0) ==
          FeedResult::NeedMore);
    CHECK(assembler.feed(0, chunk.data(), chunk.size(), 0) == FeedResult::HardFailure);
}

TEST_CASE("WsClient methods throw before connect and close is idempotent")
{
    WsClient client;
    CHECK_FALSE(client.isConnected());

    std::string out;
    CHECK_THROWS_AS(client.sendText("hi"), std::runtime_error);
    CHECK_THROWS_AS(client.receive(out, 10), std::runtime_error);

    client.close();
    client.close();
    CHECK_FALSE(client.isConnected());
}

TEST_CASE("WsClient connect to a closed local port fails without staying connected")
{
    WsClient client;

    CHECK_THROWS_AS(client.connect("ws://127.0.0.1:1", "wfm"), std::runtime_error);
    CHECK_FALSE(client.isConnected());
}

TEST_CASE("WsClient rejects a second connect without close")
{
    LocalWsUpgradeServer server;
    WsClient client;

    client.connect(server.url(), "wfm");
    CHECK(client.isConnected());

    CHECK_THROWS_AS(client.connect(server.url(), "wfm"), std::runtime_error);
    CHECK(client.isConnected());

    client.close();
    CHECK_FALSE(client.isConnected());
}

TEST_CASE("WsClient destructor drops a live connection without close")
{
    {
        LocalWsUpgradeServer server;
        WsClient client;
        client.connect(server.url(), "wfm");
        CHECK(client.isConnected());
    }

    LocalWsUpgradeServer server;
    WsClient client;
    client.connect(server.url(), "wfm");
    CHECK(client.isConnected());
}

TEST_CASE("WsClient move-assignment over a live connection frees the handle first")
{
    LocalWsUpgradeServer kept;
    LocalWsUpgradeServer dropped;
    WsClient client;
    client.connect(dropped.url(), "wfm");
    CHECK(client.isConnected());

    WsClient replacement;
    replacement.connect(kept.url(), "wfm");
    client = std::move(replacement);

    CHECK(client.isConnected());
    CHECK_FALSE(replacement.isConnected());
}

TEST_CASE("WfmSocket destructor drops a live WsClient without close")
{
    // WfmSocket::connect() always dials WS_API. Open the WsClient against the
    // local upgrade server, then move it in so ~WfmSocket runs ~WsClient
    // without close().
    {
        LocalWsUpgradeServer server;
        auto client = std::make_unique<WsClient>();
        client->connect(server.url(), "wfm");
        WfmSocket socket(std::move(client));
        CHECK(socket.isConnected());
    }

    LocalWsUpgradeServer server;
    WsClient client;
    client.connect(server.url(), "wfm");
    CHECK(client.isConnected());
}
