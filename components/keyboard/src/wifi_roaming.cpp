#include "keyboard/wifi_roaming.h"

namespace ai_keyboard {

bool has_protected_wifi_profile(const WifiProfileList& profiles) {
  for (const auto& profile : profiles) {
    if (profile.configured() && !profile.password.empty()) {
      return true;
    }
  }
  return false;
}

WifiRoamingCandidate consider_wifi_roaming_candidate(
    const WifiProfileList& profiles,
    std::string_view visible_ssid,
    int rssi,
    WifiRoamingCandidate current) {
  if (visible_ssid.empty() || (current.valid() && rssi <= current.rssi)) {
    return current;
  }
  for (std::size_t index = 0U; index < profiles.size(); ++index) {
    const auto& profile = profiles[index];
    if (!profile.configured() || profile.password.empty() ||
        std::string_view(profile.ssid) != visible_ssid) {
      continue;
    }
    return {index, rssi};
  }
  return current;
}

}  // namespace ai_keyboard
