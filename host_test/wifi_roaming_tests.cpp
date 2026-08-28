#include <cassert>

#include "keyboard/wifi_roaming.h"

int main() {
  ai_keyboard::WifiProfileList profiles{};
  assert(!ai_keyboard::has_protected_wifi_profile(profiles));
  profiles[0] = {"Home", "home-password"};
  profiles[1] = {"Open", ""};
  profiles[2] = {"Office", "office-password"};
  assert(ai_keyboard::has_protected_wifi_profile(profiles));

  auto candidate = ai_keyboard::consider_wifi_roaming_candidate(
      profiles, "Unknown", -20);
  assert(!candidate.valid());

  candidate = ai_keyboard::consider_wifi_roaming_candidate(
      profiles, "Open", -10, candidate);
  assert(!candidate.valid());

  candidate = ai_keyboard::consider_wifi_roaming_candidate(
      profiles, "Home", -70, candidate);
  assert(candidate.valid());
  assert(candidate.profile_index == 0U);
  assert(candidate.rssi == -70);

  candidate = ai_keyboard::consider_wifi_roaming_candidate(
      profiles, "Office", -45, candidate);
  assert(candidate.profile_index == 2U);
  assert(candidate.rssi == -45);

  candidate = ai_keyboard::consider_wifi_roaming_candidate(
      profiles, "Home", -60, candidate);
  assert(candidate.profile_index == 2U);
  assert(candidate.rssi == -45);
  return 0;
}
