#pragma once

#include <cstdint>

#include "keyboard/keymap.h"

namespace ai_keyboard {

enum class MusicModeState : std::uint8_t {
  Normal,
  MusicPlayback,
};

enum class MusicModeAction : std::uint8_t {
  None,
  EnterMusic,
  ExitMusic,
  PlayOrNext,
  TogglePause,
  VolumeChanged,
};

struct MusicModeEvent {
  InputId input = InputId::Count;
  InputPhase phase = InputPhase::Pressed;
  int encoder_step = 0;
  std::uint32_t timestamp_ms = 0;
};

struct MusicModeResult {
  MusicModeAction action = MusicModeAction::None;
  MusicModeState state = MusicModeState::Normal;
  std::uint8_t volume_percent = 15;
  bool consumed = false;
};

// Input-only state machine. It owns no transport, audio, LED, or HID state;
// platform code consumes the returned action and performs those side effects.
class MusicModeController {
 public:
  // A fast double click enters/exits music mode. The slower brightness
  // gesture remains available through its existing timing window.
  static constexpr std::uint32_t kMusicDoubleClickWindowMs = 250U;
  static constexpr std::uint8_t kMusicDoubleClickCount = 2U;
  static constexpr std::uint8_t kDefaultVolumePercent = 15U;
  static constexpr std::uint8_t kVolumeStepPercent = 5U;

  MusicModeResult handle(const MusicModeEvent& event);
  bool double_click_pending() const { return encoder_click_count_ != 0; }
  bool double_click_pending(std::uint32_t now_ms) const {
    return encoder_click_count_ != 0 &&
           static_cast<std::uint32_t>(now_ms - last_encoder_press_ms_) <=
               kMusicDoubleClickWindowMs;
  }
  bool suppress_encoder_release() const { return suppress_encoder_release_; }
  MusicModeState state() const { return state_; }
  std::uint8_t volume_percent() const { return volume_percent_; }
  bool paused() const { return paused_; }
  void reset();

 private:
  MusicModeResult result(MusicModeAction action, bool consumed) const;

  MusicModeState state_ = MusicModeState::Normal;
  std::uint8_t volume_percent_ = kDefaultVolumePercent;
  std::uint8_t encoder_click_count_ = 0;
  std::uint32_t last_encoder_press_ms_ = 0;
  bool suppress_encoder_release_ = false;
  bool paused_ = false;
};

}  // namespace ai_keyboard
