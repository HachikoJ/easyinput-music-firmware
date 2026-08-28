#pragma once

#include <cstdint>
#include <string>

#include "keyboard/keymap.h"

namespace ai_keyboard {

// Board-side control state for online voice search. Network and audio tasks
// report results back as events; this class owns only timing and input policy.
enum class OnlineMusicState : std::uint8_t {
  Normal,
  WaitingForCommand,
  Preparing,
  Listening,
  Resolving,
  Playing,
  WaitingNextSong,
  RetryWindow,
};

enum class OnlineMusicAction : std::uint8_t {
  None,
  EnterMode,
  ExitMode,
  StartListening,
  ListeningReady,
  StopListening,
  StartResolving,
  StartPlayback,
  RetryListening,
  TogglePause,
  VolumeChanged,
  PlaybackReady,
  PlaybackCompleted,
  AutoPlayNext,
  PlaybackFailed,
  SessionExpired,
  FailureBeep3,
};

struct OnlineMusicEvent {
  enum class Kind : std::uint8_t {
    EncoderPress,
    EncoderRelease,
    EncoderTurn,
    KeyPress,
    KeyRelease,
    CaptureStarted,
    RecognitionSucceeded,
    RecognitionFailed,
    PlaybackSucceeded,
    PlaybackCompleted,
    PlaybackFailed,
    Tick,
  };

  Kind kind = Kind::Tick;
  int encoder_step = 0;
  std::uint32_t timestamp_ms = 0;
  std::string recognized_text;
};

struct OnlineMusicResult {
  OnlineMusicAction action = OnlineMusicAction::None;
  OnlineMusicState state = OnlineMusicState::Normal;
  std::uint8_t volume_percent = 15;
  bool paused = false;
  bool consumed = false;
  bool listening_deadline_reached = false;
  bool retry_deadline_reached = false;
  bool next_song_deadline_reached = false;
  bool session_deadline_reached = false;
  std::string recognized_text;
};

class OnlineMusicModeController {
 public:
  static constexpr std::uint32_t kDoubleClickWindowMs = 500U;
  // Fast double-click stays reserved for the existing offline music mode.
  static constexpr std::uint32_t kOnlineDoubleClickMinMs = 251U;
  static constexpr std::uint32_t kListeningWindowMs = 10000U;
  static constexpr std::uint32_t kRetryWindowMs = 30000U;
  static constexpr std::uint32_t kNextSongWindowMs = 30000U;
  static constexpr std::uint32_t kSessionWindowMs = 30U * 60U * 1000U;
  static constexpr std::uint8_t kDefaultVolumePercent = 15U;
  static constexpr std::uint8_t kVolumeStepPercent = 5U;

  OnlineMusicResult handle(const OnlineMusicEvent& event);

  OnlineMusicState state() const { return state_; }
  std::uint8_t volume_percent() const { return volume_percent_; }
  bool paused() const { return paused_; }
  bool active() const { return state_ != OnlineMusicState::Normal; }
  bool listening() const { return state_ == OnlineMusicState::Listening; }
  bool resolving() const { return state_ == OnlineMusicState::Resolving; }
  bool playing() const { return state_ == OnlineMusicState::Playing; }
  bool waiting_next_song() const {
    return state_ == OnlineMusicState::WaitingNextSong;
  }
  bool retry_window() const { return state_ == OnlineMusicState::RetryWindow; }
  bool click_pending() const { return encoder_click_count_ != 0U; }
  bool click_pending(std::uint32_t now_ms) const {
    return encoder_click_count_ != 0U &&
           static_cast<std::uint32_t>(now_ms - last_encoder_press_ms_) <=
               kDoubleClickWindowMs;
  }
  bool next_deadline_ms(std::uint32_t* deadline_ms) const;
  void reset();

 private:
  OnlineMusicResult result(OnlineMusicAction action, bool consumed) const;
  OnlineMusicResult enter_listening(std::uint32_t now_ms);
  OnlineMusicResult stop_listening();
  OnlineMusicResult exit_mode();

  OnlineMusicState state_ = OnlineMusicState::Normal;
  std::uint8_t volume_percent_ = kDefaultVolumePercent;
  bool paused_ = false;
  std::uint8_t encoder_click_count_ = 0;
  std::uint32_t last_encoder_press_ms_ = 0;
  std::uint32_t listening_started_ms_ = 0;
  std::uint32_t retry_started_ms_ = 0;
  std::uint32_t next_song_started_ms_ = 0;
  std::uint32_t session_deadline_ms_ = 0;
  bool session_active_ = false;
  bool suppress_encoder_release_ = false;
};

}  // namespace ai_keyboard
