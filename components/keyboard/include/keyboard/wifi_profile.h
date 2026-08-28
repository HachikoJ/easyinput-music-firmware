#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace ai_keyboard {

inline constexpr std::size_t kMaxWifiProfiles = 5U;

struct WifiProfile {
  std::string ssid;
  std::string password;

  bool configured() const { return !ssid.empty(); }
};

using WifiProfileList = std::array<WifiProfile, kMaxWifiProfiles>;

}  // namespace ai_keyboard
