#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>
#include <chrono>
#include <thread>
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class HttpClient
{
public:
    //=======================================================http construct and destruct============================================================================================================
    HttpClient() : lastRequest(std::chrono::steady_clock::now() - minDelay) { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~HttpClient() { curl_global_cleanup(); }
    //=======================================================http construct and destruct============================================================================================================

    //=======================================================http payload methods============================================================================================================
    std::string post(const std::string &url, const json &payload, const std::map<std::string, std::string> &headers, std::map<std::string, std::string> &response_headers)
    {
        // Apply rate limiting
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest);
        if (elapsed < minDelay)
        {
            std::this_thread::sleep_for(minDelay - elapsed);
        }
        lastRequest = std::chrono::steady_clock::now();

        CURL *curl = curl_easy_init();
        if (!curl)
            throw std::runtime_error("Failed to initialize curl");

        std::string response_body;
        struct curl_slist *curl_headers = nullptr;
        for (const auto &h : headers)
        {
            curl_headers = curl_slist_append(curl_headers, (h.first + ": " + h.second).c_str());
        }

        std::string body = payload.dump();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);

        long http_code = 0;
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            curl_slist_free_all(curl_headers);
            curl_easy_cleanup(curl);
            throw std::runtime_error("Curl error: " + std::string(curl_easy_strerror(res)));
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code < 200 || http_code >= 300)
        {
            curl_slist_free_all(curl_headers);
            curl_easy_cleanup(curl);
            throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response_body);
        }

        curl_slist_free_all(curl_headers);
        curl_easy_cleanup(curl);
        return response_body;
    }
    std::string patch(const std::string &url, const json &payload, const std::map<std::string, std::string> &headers, std::map<std::string, std::string> &response_headers)
    {
        // Apply rate limiting
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest);
        if (elapsed < minDelay)
        {
            std::this_thread::sleep_for(minDelay - elapsed);
        }
        lastRequest = std::chrono::steady_clock::now();

        CURL *curl = curl_easy_init();
        if (!curl)
            throw std::runtime_error("Failed to initialize curl");

        std::string response_body;
        struct curl_slist *curl_headers = nullptr;
        for (const auto &h : headers)
        {
            curl_headers = curl_slist_append(curl_headers, (h.first + ": " + h.second).c_str());
        }

        std::string body = payload.dump();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);

        long http_code = 0;
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            curl_slist_free_all(curl_headers);
            curl_easy_cleanup(curl);
            throw std::runtime_error("Curl error: " + std::string(curl_easy_strerror(res)));
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code < 200 || http_code >= 300)
        {
            curl_slist_free_all(curl_headers);
            curl_easy_cleanup(curl);
            throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response_body);
        }

        curl_slist_free_all(curl_headers);
        curl_easy_cleanup(curl);
        return response_body;
    }
    //=======================================================http payload methods============================================================================================================

    //=======================================================printing debugs=======================================================================================================
    void debugPrintHeaders(const std::map<std::string, std::string> &headers)
    {
        std::cout << "Headers as curl_slist:" << "\n";
        for (const auto &h : headers)
        {
            std::cout << h.first << ": " << h.second << "\n";
        }
    }
    //=======================================================printing debugs=======================================================================================================

    //=======================================================http no payload methods =======================================================================================================
    std::string Get(const std::string &url, const std::map<std::string, std::string> &headers)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest);

        if (elapsed < minDelay)
        {
            std::this_thread::sleep_for(minDelay - elapsed);
        }

        lastRequest = std::chrono::steady_clock::now();

        CURL *handle = curl_easy_init();
        if (!handle)
        {
            throw std::runtime_error("Failed to init curl handle");
        }

        std::string response;

        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

        curl_slist *header = nullptr;
        for (const auto &h : headers)
        {
            std::string header_line = h.first + ": " + h.second;
            header = curl_slist_append(header, header_line.c_str());
        }

        // debugPrintHeaders(headers);

        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);

        CURLcode result = curl_easy_perform(handle);

        if (result != CURLE_OK)
        {
            curl_slist_free_all(header);
            curl_easy_cleanup(handle);
            throw std::runtime_error(curl_easy_strerror(result));
        }

        curl_slist_free_all(header);
        curl_easy_cleanup(handle);

        return response;
    }
    long Delete(const std::string &url, const std::map<std::string, std::string> &headers)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest);

        if (elapsed < minDelay)
        {
            std::this_thread::sleep_for(minDelay - elapsed);
        }

        lastRequest = std::chrono::steady_clock::now();

        CURL *handle = curl_easy_init();
        if (!handle)
        {
            throw std::runtime_error("Failed to init curl handle");
        }

        std::string response_body;  // Capture response to prevent stdout output
        long http_code = 0;

        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_body);

        curl_slist *header = nullptr;
        for (const auto &h : headers)
        {
            std::string header_line = h.first + ": " + h.second;
            header = curl_slist_append(header, header_line.c_str());
        }

        // debugPrintHeaders(headers);

        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);

        CURLcode result = curl_easy_perform(handle);

        if (result != CURLE_OK)
        {
            curl_slist_free_all(header);
            curl_easy_cleanup(handle);
            throw std::runtime_error(curl_easy_strerror(result));
        }

        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header);
        curl_easy_cleanup(handle);

        return http_code;
    }
    //=======================================================http no payload methods =======================================================================================================

private:
    //=======================================================http data member =======================================================================================================
    std::chrono::milliseconds minDelay{350};
    std::chrono::steady_clock::time_point lastRequest;
    //=======================================================http data member =======================================================================================================

    //=======================================================http callback =======================================================================================================

    static size_t write_callback(void *contents, size_t size, size_t nmemb, std::string *output)
    {
        output->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }
    static size_t header_callback(char *buffer, size_t size, size_t nitems, std::map<std::string, std::string> *headers)
    {
        std::string header(buffer, size * nitems);
        auto pos = header.find(':');
        if (pos != std::string::npos)
        {
            std::string key = header.substr(0, pos);
            std::string value = header.substr(pos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            (*headers)[key] = value;
        }
        return size * nitems;
    }
    //=======================================================http callback =======================================================================================================
};

#endif // HTTP_CLIENT_H
