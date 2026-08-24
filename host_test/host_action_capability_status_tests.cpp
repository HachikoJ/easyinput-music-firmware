#include <cassert>
#include <cstdint>
#include <string>

#include "keyboard/ble_status_wire.h"
#include "keyboard/config_status.h"

namespace {

void assert_capability(const std::string& json) {
  const std::string key = R"("host_action_v1":true)";
  assert(json.find(key) != std::string::npos);
  assert(json.find(key, json.find(key) + 1) == std::string::npos);
  assert(json.find(R"("host_action_v1":"true")") == std::string::npos);
  assert(json.size() <= ai_keyboard::kConfigStatusGattSafeLen);
}

}  // namespace

int main() {
  ai_keyboard::ConfigStatusSnapshot snapshot;
  snapshot.firmware = "0.8.0";
  snapshot.phase = "push";
  snapshot.status = "ok";
  snapshot.saved = true;
  snapshot.target_platform = "windows";

  const auto confirmation =
      ai_keyboard::build_config_confirmation_status_json(snapshot);
  assert_capability(confirmation);
  assert(confirmation.size() == 382);

  snapshot.phase = "battery";
  const auto battery = ai_keyboard::build_config_status_json(snapshot);
  assert_capability(battery);
  assert(battery.size() == 306);
  const auto battery_ble = ai_keyboard::append_ble_status_wire_json(
      battery, {true, true, UINT16_MAX, UINT16_MAX, UINT16_MAX});
  assert_capability(battery_ble);
  assert(battery_ble.size() == 383);

  ai_keyboard::SpeakerProbeSnapshot speaker;
  speaker.present = true;
  snapshot.phase = "spk_probe";
  snapshot.speaker = &speaker;
  const auto probe = ai_keyboard::build_config_status_json(snapshot);
  assert_capability(probe);
  assert(probe.size() == 355);
  assert(probe.find(R"("firmware":"0.8.0")") != std::string::npos);
  return 0;
}
