#ifndef CURL_RUNTIME_H
#define CURL_RUNTIME_H

#include <curl/curl.h>

/**
 * @file curl_runtime.h
 * @brief Process-wide libcurl global initialisation.
 *
 * libcurl's global state is shared by every handle in the process. Calling
 * `curl_global_init` in one client's constructor and `curl_global_cleanup` in
 * its destructor breaks as soon as a second client exists: the first
 * destructor invalidates handles the second one still uses. Initialise once
 * and never clean up; the process exit tears the state down with everything
 * else.
 */
inline void wftEnsureCurlGlobalInit()
{
    static const bool initialised = []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)initialised;
}

#endif // CURL_RUNTIME_H
