#ifndef API_ERROR_H
#define API_ERROR_H

#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

/**
 * @file api_error.h
 * @brief Parsing for the Warframe.Market v2 error envelope.
 *
 * Documented failures carry machine-readable error codes rather than prose:
 *
 * @code
 * {"data": null, "error": {"request": ["app.item.notFound"]}}
 * @endcode
 *
 * Field-level validation failures add an `inputs` map:
 *
 * @code
 * {"data": null, "error": {"request": [], "inputs": {"platinum": "app.field.tooSmall"}}}
 * @endcode
 *
 * The overview page is ambiguous about whether `request`/`inputs` sit inside
 * `error` or at the top level, and live traffic nests them under `error`. This
 * parser accepts both so a doc revision cannot silently break error reporting.
 */
class ApiError : public std::runtime_error
{
public:
    explicit ApiError(const std::string &message)
        : std::runtime_error(message) {}

    ApiError(const std::string &message,
             std::vector<std::string> request_codes,
             std::vector<std::string> input_codes)
        : std::runtime_error(message),
          requestCodes_(std::move(request_codes)),
          inputCodes_(std::move(input_codes)) {}

    /// Request-level error codes, e.g. `app.errors.unauthorized`.
    const std::vector<std::string> &requestCodes() const { return requestCodes_; }

    /// `field=code` pairs such as `platinum=app.field.tooSmall`.
    const std::vector<std::string> &inputCodes() const { return inputCodes_; }

private:
    std::vector<std::string> requestCodes_;
    std::vector<std::string> inputCodes_;
};

namespace api_error_detail
{

inline void collectStrings(const nlohmann::json &node, std::vector<std::string> &out)
{
    if (node.is_array())
    {
        for (const auto &entry : node)
        {
            if (entry.is_string())
            {
                out.push_back(entry.get<std::string>());
            }
        }
    }
    else if (node.is_string())
    {
        out.push_back(node.get<std::string>());
    }
}

inline void collectInputs(const nlohmann::json &node, std::vector<std::string> &out)
{
    if (!node.is_object())
    {
        return;
    }
    for (auto it = node.begin(); it != node.end(); ++it)
    {
        if (it.value().is_string())
        {
            out.push_back(it.key() + "=" + it.value().get<std::string>());
        }
    }
}

} // namespace api_error_detail

/**
 * @brief Throw ApiError if `root` carries a v2 error payload.
 *
 * Accepts the error object both nested under `error` and at the top level.
 * Returns quietly when the payload reports no error, which includes the
 * success case where `error` is `null`.
 */
inline void throwIfApiError(const nlohmann::json &root, const std::string &context)
{
    if (!root.is_object())
    {
        return;
    }

    const nlohmann::json *payload = nullptr;
    if (root.contains("error") && root.at("error").is_object())
    {
        payload = &root.at("error");
    }
    else if (root.contains("request") || root.contains("inputs"))
    {
        payload = &root;
    }

    if (payload == nullptr)
    {
        return;
    }

    std::vector<std::string> request_codes;
    std::vector<std::string> input_codes;
    if (payload->contains("request"))
    {
        api_error_detail::collectStrings(payload->at("request"), request_codes);
    }
    if (payload->contains("inputs"))
    {
        api_error_detail::collectInputs(payload->at("inputs"), input_codes);
    }

    if (request_codes.empty() && input_codes.empty())
    {
        return;
    }

    std::string message = context + ": ";
    bool first = true;
    for (const auto &code : request_codes)
    {
        message += (first ? "" : ", ") + code;
        first = false;
    }
    for (const auto &code : input_codes)
    {
        message += (first ? "" : ", ") + code;
        first = false;
    }

    throw ApiError(message, std::move(request_codes), std::move(input_codes));
}

#endif // API_ERROR_H
