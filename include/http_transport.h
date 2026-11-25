#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file http_transport.h
 * @brief Abstract HTTP transport used by MarketAPI.
 *
 * Exists so reads can be exercised without a network: tests implement this
 * interface with canned responses. HttpClient is the production implementation.
 */

/**
 * @brief Case-insensitive ordering for HTTP header names.
 *
 * HTTP header names are case-insensitive, so a lookup for "content-type"
 * must find a header the server spelled "Content-Type". The
 * `is_transparent` marker makes `find`/`contains`/`at` accept a `string_view`
 * or a literal without building a `std::string` first.
 */
struct CaseInsensitiveLess
{
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const
    {
        return std::ranges::lexicographical_compare(a, b, {}, lower, lower);
    }

private:
    static char lower(char value)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }
};

/**
 * @brief Thrown by a transport when the server answers with a non-2xx status.
 *
 * Carries the status and the raw body so the API layer can parse the v2 error
 * envelope instead of surfacing a bare status code.
 */
class HttpStatusError : public std::runtime_error
{
public:
    HttpStatusError(long status, std::string body)
        : std::runtime_error(std::format("HTTP {}: {}", status, body)),
          status_(status), body_(std::move(body)) {}

    [[nodiscard]] long status() const { return status_; }
    [[nodiscard]] const std::string &body() const { return body_; }

private:
    long status_;
    std::string body_;
};

class HttpTransport
{
public:
    using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

    virtual ~HttpTransport() = default;

    virtual std::string Get(const std::string &url, const Headers &headers) = 0;
};

#endif // HTTP_TRANSPORT_H
