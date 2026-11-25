#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string_view>

// ==================== API Base URLs ====================
inline constexpr std::string_view V2_API = "https://api.warframe.market/v2";
inline constexpr std::string_view WS_API = "wss://ws.warframe.market/socket";

/**
 * @brief Version of this library, sent as the User-Agent.
 *
 * The Warframe.Market API rules require every client to identify itself and
 * give the operators a way to classify its traffic. Keep the contact URL
 * pointing at the repository so they can reach the maintainer.
 */
inline constexpr std::string_view WFT_USER_AGENT = "wft/0.2.0 (+https://github.com/tonekode/wft)";

#endif // CONSTANTS_H
