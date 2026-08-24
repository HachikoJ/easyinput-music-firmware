#include <array>
#include <cassert>
#include <string>

#include "keyboard/config_payload.h"

int main() {
  constexpr const char* kUuid =
      "123e4567-e89b-12d3-a456-426614174000";
  std::string payload = R"({"schema":"ai_keyboard.v1","profiles":[{"id":"default","keys":{
    "KEY1":{"press":"host_action:UUID"},"KEY2":{"press":"host_action:UUID"},
    "KEY3":{"press":"host_action:UUID"},"KEY4":{"press":"host_action:UUID"},
    "KEY5":{"press":"host_action:UUID"},"KEY6":{"press":"host_action:UUID"},
    "KEY7":{"press":"host_action:UUID"},"KEY8":{"press":"host_action:UUID"}
  },"encoder":{"left":"disabled","right":"disabled","press":"disabled"}}]})";
  const std::string replacement = std::string("host_action:") + kUuid;
  std::size_t offset = 0;
  while ((offset = payload.find("host_action:UUID", offset)) != std::string::npos) {
    payload.replace(offset, 16, replacement);
    offset += replacement.size();
  }

  const auto result = ai_keyboard::parse_config_payload(payload);
  assert(result.status == ai_keyboard::ConfigParseStatus::Ok);
  const std::array keys{
      ai_keyboard::InputId::Key1, ai_keyboard::InputId::Key2,
      ai_keyboard::InputId::Key3, ai_keyboard::InputId::Key4,
      ai_keyboard::InputId::Key5, ai_keyboard::InputId::Key6,
      ai_keyboard::InputId::Key7, ai_keyboard::InputId::Key8};
  for (const auto key : keys) {
    const auto& action = result.config.keymap.action_for(key);
    assert(action.kind == ai_keyboard::ActionKind::HostAction);
    assert(action.text == replacement);
    const auto press = ai_keyboard::event_for_action(
        action, ai_keyboard::InputPhase::Pressed, "", "",
        ai_keyboard::HostPlatform::MacOS);
    const auto release = ai_keyboard::event_for_action(
        action, ai_keyboard::InputPhase::Released, "", "",
        ai_keyboard::HostPlatform::MacOS);
    assert(press.kind == ai_keyboard::FirmwareEventKind::AppCommand);
    assert(press.host_action);
    assert(press.value == kUuid);
    assert(release.kind == ai_keyboard::FirmwareEventKind::None);
  }
  return 0;
}
