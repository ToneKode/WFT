#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <map>
#include <string>
#include <nlohmann/json.hpp>

/**
 * @file http_transport.h
 * @brief Abstract HTTP transport used by WarframeMarketAPI.
 *
 * Exists so the API layer can be exercised without a network or a live
 * Warframe.Market account: tests implement this interface with canned
 * responses. HttpClient is the production implementation.
 */
class HttpTransport
{
public:
    using Headers = std::map<std::string, std::string>;

    virtual ~HttpTransport() {}

    virtual std::string post(const std::string &url,
                             const nlohmann::json &payload,
                             const Headers &headers,
                             Headers &response_headers) = 0;

    virtual std::string patch(const std::string &url,
                              const nlohmann::json &payload,
                              const Headers &headers,
                              Headers &response_headers) = 0;

    virtual std::string Get(const std::string &url, const Headers &headers) = 0;

    virtual long Delete(const std::string &url, const Headers &headers) = 0;
};

#endif // HTTP_TRANSPORT_H
