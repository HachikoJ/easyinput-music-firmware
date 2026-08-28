#pragma once

#include <cstddef>
#include <string_view>

#include "keyboard/wifi_profile.h"

namespace ai_keyboard {

bool has_protected_wifi_profile(const WifiProfileList& profiles);

struct WifiRoamingCandidate {
  std::size_t profile_index = kMaxWifiProfiles;
  int rssi = -128;

  bool valid() const { return profile_index < kMaxWifiProfiles; }
};

WifiRoamingCandidate consider_wifi_roaming_candidate(
    const WifiProfileList& profiles,
    std::string_view visible_ssid,
    int rssi,
    WifiRoamingCandidate current = {});

}  // namespace ai_keyboard
