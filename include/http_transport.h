#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>

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
 * must find a header the server spelled "Content-Type".
 */
struct CaseInsensitiveLess
{
    bool operator()(const std::string &a, const std::string &b) const
    {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](char lhs, char rhs)
            {
                return std::tolower(static_cast<unsigned char>(lhs)) <
                       std::tolower(static_cast<unsigned char>(rhs));
            });
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
    HttpStatusError(long status, const std::string &body)
        : std::runtime_error("HTTP " + std::to_string(status) + ": " + body),
          status_(status), body_(body) {}

    long status() const { return status_; }
    const std::string &body() const { return body_; }

private:
    long status_;
    std::string body_;
};

class HttpTransport
{
public:
    using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

    virtual ~HttpTransport() {}

    virtual std::string Get(const std::string &url, const Headers &headers) = 0;
};

#endif // HTTP_TRANSPORT_H
