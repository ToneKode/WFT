#ifndef API_ERROR_H
#define API_ERROR_H

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    [[nodiscard]] const std::vector<std::string> &requestCodes() const
    {
        return requestCodes_;
    }

    /// `field=code` pairs such as `platinum=app.field.tooSmall`.
    [[nodiscard]] const std::vector<std::string> &inputCodes() const { return inputCodes_; }

private:
    std::vector<std::string> requestCodes_;
    std::vector<std::string> inputCodes_;
};

namespace api_error_detail
{

/// Append every string in `node`, whether it is a bare string or an array.
inline void collectStrings(const nlohmann::json &node, std::vector<std::string> &out)
{
    if (node.is_string())
    {
        out.push_back(node.get<std::string>());
        return;
    }
    if (!node.is_array())
    {
        return;
    }

    for (const auto &entry : node)
    {
        if (entry.is_string())
        {
            out.push_back(entry.get<std::string>());
        }
    }
}

/// Append `key=value` for each string-valued entry of an `inputs` object.
inline void collectInputs(const nlohmann::json &node, std::vector<std::string> &out)
{
    if (!node.is_object())
    {
        return;
    }

    for (const auto &[field, value] : node.items())
    {
        if (value.is_string())
        {
            out.push_back(std::format("{}={}", field, value.get<std::string>()));
        }
    }
}

/// Join codes into one readable message body: `a, b, c`.
inline std::string joinCodes(const std::vector<std::string> &request_codes,
                             const std::vector<std::string> &input_codes)
{
    std::vector<std::string_view> all;
    all.reserve(request_codes.size() + input_codes.size());
    all.insert(all.end(), request_codes.begin(), request_codes.end());
    all.insert(all.end(), input_codes.begin(), input_codes.end());

    std::string joined;
    for (const std::string_view code : all)
    {
        if (!joined.empty())
        {
            joined += ", ";
        }
        joined += code;
    }
    return joined;
}

} // namespace api_error_detail

/**
 * @brief Throw ApiError if `root` carries a v2 error payload.
 *
 * Accepts the error object both nested under `error` and at the top level.
 * Returns quietly when the payload reports no error, which includes the
 * success case where `error` is `null`.
 */
inline void throwIfApiError(const nlohmann::json &root, std::string_view context)
{
    if (!root.is_object())
    {
        return;
    }

    const nlohmann::json *payload = nullptr;
    if (const auto error = root.find("error"); error != root.end() && error->is_object())
    {
        payload = &*error;
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
    if (const auto request = payload->find("request"); request != payload->end())
    {
        api_error_detail::collectStrings(*request, request_codes);
    }
    if (const auto inputs = payload->find("inputs"); inputs != payload->end())
    {
        api_error_detail::collectInputs(*inputs, input_codes);
    }

    if (request_codes.empty() && input_codes.empty())
    {
        return;
    }

    // Build the message into a local first: format arguments and the two
    // std::move arguments of the throw below have unspecified evaluation order,
    // so reading the codes inside the throw expression could see them already
    // moved from.
    std::string message = std::format("{}: {}", context,
                                      api_error_detail::joinCodes(request_codes, input_codes));
    throw ApiError(message, std::move(request_codes), std::move(input_codes));
}

#endif // API_ERROR_H
