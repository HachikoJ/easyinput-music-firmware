#include "keyboard/music_mode.h"

#include <algorithm>

namespace ai_keyboard {

void MusicModeController::reset() {
  state_ = MusicModeState::Normal;
  volume_percent_ = kDefaultVolumePercent;
  encoder_click_count_ = 0;
  last_encoder_press_ms_ = 0;
  suppress_encoder_release_ = false;
  paused_ = false;
}

MusicModeResult MusicModeController::result(
    MusicModeAction action,
    bool consumed) const {
  return {action, state_, volume_percent_, consumed};
}

MusicModeResult MusicModeController::handle(const MusicModeEvent& event) {
  if (event.input == InputId::EncoderPress &&
      event.phase == InputPhase::Pressed) {
    const bool second_click =
        encoder_click_count_ != 0 &&
        static_cast<std::uint32_t>(event.timestamp_ms -
                                   last_encoder_press_ms_) <=
            kMusicDoubleClickWindowMs;
    if (!second_click) {
      encoder_click_count_ = 1;
    } else {
      ++encoder_click_count_;
    }
    last_encoder_press_ms_ = event.timestamp_ms;
    if (encoder_click_count_ >= kMusicDoubleClickCount) {
      encoder_click_count_ = 0;
      suppress_encoder_release_ = true;
      if (state_ == MusicModeState::Normal) {
        state_ = MusicModeState::MusicPlayback;
        volume_percent_ = kDefaultVolumePercent;
        paused_ = false;
        return result(MusicModeAction::EnterMusic, true);
      }
      state_ = MusicModeState::Normal;
      paused_ = false;
      return result(MusicModeAction::ExitMusic, true);
    }
  }

  if (event.input == InputId::EncoderPress &&
      event.phase == InputPhase::Released && suppress_encoder_release_) {
    suppress_encoder_release_ = false;
    return result(MusicModeAction::None, true);
  }

  if (state_ == MusicModeState::Normal) {
    return result(MusicModeAction::None, false);
  }

  if (event.phase == InputPhase::Pressed && event.input >= InputId::Key1 &&
      event.input <= InputId::Key8) {
    paused_ = false;
    return result(MusicModeAction::PlayOrNext, true);
  }

  if (event.input == InputId::EncoderPress &&
      event.phase == InputPhase::Released) {
    paused_ = !paused_;
    return result(MusicModeAction::TogglePause, true);
  }

  const bool encoder_turn = event.input == InputId::EncoderLeft ||
                            event.input == InputId::EncoderRight;
  if (encoder_turn && event.encoder_step != 0) {
    const int magnitude = event.encoder_step < 0 ? -event.encoder_step
                                                 : event.encoder_step;
    const int detent_delta = event.encoder_step < 0 ? magnitude : -magnitude;
    const auto adjusted = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(volume_percent_) +
            detent_delta * static_cast<int>(kVolumeStepPercent),
        0,
        100));
    if (adjusted != volume_percent_) {
      volume_percent_ = adjusted;
      return result(MusicModeAction::VolumeChanged, true);
    }
  }

  return result(MusicModeAction::None, true);
}

}  // namespace ai_keyboard
