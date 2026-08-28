#include "keyboard/online_music_mode.h"

#include <algorithm>

namespace ai_keyboard {

void OnlineMusicModeController::reset() {
  state_ = OnlineMusicState::Normal;
  volume_percent_ = kDefaultVolumePercent;
  paused_ = false;
  encoder_click_count_ = 0;
  last_encoder_press_ms_ = 0;
  listening_started_ms_ = 0;
  retry_started_ms_ = 0;
  next_song_started_ms_ = 0;
  session_deadline_ms_ = 0;
  session_active_ = false;
  suppress_encoder_release_ = false;
}

OnlineMusicResult OnlineMusicModeController::result(
    OnlineMusicAction action,
    bool consumed) const {
  return {action, state_, volume_percent_, paused_, consumed, false, false,
          false, false, {}};
}

OnlineMusicResult OnlineMusicModeController::enter_listening(
    std::uint32_t now_ms) {
  if (session_active_) {
    session_deadline_ms_ = now_ms + kSessionWindowMs;
  }
  state_ = OnlineMusicState::Preparing;
  listening_started_ms_ = 0U;
  return result(OnlineMusicAction::StartListening, true);
}

OnlineMusicResult OnlineMusicModeController::stop_listening() {
  state_ = OnlineMusicState::Resolving;
  listening_started_ms_ = 0;
  return result(OnlineMusicAction::StartResolving, true);
}

OnlineMusicResult OnlineMusicModeController::exit_mode() {
  state_ = OnlineMusicState::Normal;
  paused_ = false;
  encoder_click_count_ = 0;
  listening_started_ms_ = 0;
  retry_started_ms_ = 0;
  next_song_started_ms_ = 0;
  session_deadline_ms_ = 0;
  session_active_ = false;
  suppress_encoder_release_ = true;
  return result(OnlineMusicAction::ExitMode, true);
}

OnlineMusicResult OnlineMusicModeController::handle(
    const OnlineMusicEvent& event) {
  if (event.kind == OnlineMusicEvent::Kind::EncoderPress) {
    const auto interval = static_cast<std::uint32_t>(
        event.timestamp_ms - last_encoder_press_ms_);
    const bool second_click =
        encoder_click_count_ != 0 &&
        interval >= kOnlineDoubleClickMinMs &&
        interval <= kDoubleClickWindowMs;
    const bool offline_click = encoder_click_count_ != 0 &&
                               interval < kOnlineDoubleClickMinMs;
    if (offline_click) {
      encoder_click_count_ = 0U;
      last_encoder_press_ms_ = event.timestamp_ms;
      return result(OnlineMusicAction::None, false);
    }
    encoder_click_count_ = second_click ? 2U : 1U;
    last_encoder_press_ms_ = event.timestamp_ms;
    if (session_active_) {
      session_deadline_ms_ = event.timestamp_ms + kSessionWindowMs;
    }
    if (encoder_click_count_ == 2U) {
      encoder_click_count_ = 0;
      suppress_encoder_release_ = true;
      if (state_ == OnlineMusicState::Normal) {
        state_ = OnlineMusicState::WaitingForCommand;
        volume_percent_ = kDefaultVolumePercent;
        paused_ = false;
        session_active_ = true;
        session_deadline_ms_ = event.timestamp_ms + kSessionWindowMs;
        return result(OnlineMusicAction::EnterMode, true);
      }
      return exit_mode();
    }
  }

  if (event.kind == OnlineMusicEvent::Kind::EncoderRelease &&
      suppress_encoder_release_) {
    suppress_encoder_release_ = false;
    return result(OnlineMusicAction::None, true);
  }

  if (state_ == OnlineMusicState::Normal) {
    return result(OnlineMusicAction::None, false);
  }

  if (event.kind == OnlineMusicEvent::Kind::EncoderRelease) {
    // A release is held until the double-click window expires. This keeps a
    // single click (pause/resume) distinguishable from the exit gesture.
    return result(OnlineMusicAction::None, true);
  }

  if (event.kind == OnlineMusicEvent::Kind::KeyPress) {
    if (session_active_) {
      session_deadline_ms_ = event.timestamp_ms + kSessionWindowMs;
    }
    if (state_ == OnlineMusicState::Listening) {
      return stop_listening();
    }
    if (state_ == OnlineMusicState::Preparing ||
        state_ == OnlineMusicState::Resolving) {
      return result(OnlineMusicAction::None, true);
    }
    return enter_listening(event.timestamp_ms);
  }

  if (event.kind == OnlineMusicEvent::Kind::KeyRelease) {
    return result(OnlineMusicAction::None, true);
  }

  if (event.kind == OnlineMusicEvent::Kind::EncoderTurn &&
      event.encoder_step != 0 && state_ == OnlineMusicState::Playing) {
    if (session_active_) {
      session_deadline_ms_ = event.timestamp_ms + kSessionWindowMs;
    }
    const int magnitude = event.encoder_step < 0 ? -event.encoder_step
                                                  : event.encoder_step;
    const int delta = event.encoder_step < 0 ? -magnitude : magnitude;
    const auto adjusted = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(volume_percent_) +
            delta * static_cast<int>(kVolumeStepPercent),
        0,
        100));
    if (adjusted != volume_percent_) {
      volume_percent_ = adjusted;
      return result(OnlineMusicAction::VolumeChanged, true);
    }
    return result(OnlineMusicAction::None, true);
  }

  switch (event.kind) {
    case OnlineMusicEvent::Kind::CaptureStarted:
      if (state_ != OnlineMusicState::Preparing) {
        return result(OnlineMusicAction::None, true);
      }
      state_ = OnlineMusicState::Listening;
      listening_started_ms_ = event.timestamp_ms;
      return result(OnlineMusicAction::ListeningReady, true);
    case OnlineMusicEvent::Kind::RecognitionSucceeded:
      if (state_ != OnlineMusicState::Resolving) {
        return result(OnlineMusicAction::None, true);
      }
      if (event.recognized_text.empty()) {
        state_ = OnlineMusicState::RetryWindow;
        retry_started_ms_ = event.timestamp_ms;
        return result(OnlineMusicAction::FailureBeep3, true);
      }
      {
        auto playback = result(OnlineMusicAction::StartPlayback, true);
        playback.recognized_text = event.recognized_text;
        return playback;
      }
    case OnlineMusicEvent::Kind::RecognitionFailed:
      if (state_ != OnlineMusicState::Preparing &&
          state_ != OnlineMusicState::Listening &&
          state_ != OnlineMusicState::Resolving) {
        return result(OnlineMusicAction::None, true);
      }
      state_ = OnlineMusicState::RetryWindow;
      retry_started_ms_ = event.timestamp_ms;
      return result(OnlineMusicAction::FailureBeep3, true);
    case OnlineMusicEvent::Kind::PlaybackSucceeded:
      if (state_ != OnlineMusicState::Resolving) {
        return result(OnlineMusicAction::None, true);
      }
      state_ = OnlineMusicState::Playing;
      paused_ = false;
      retry_started_ms_ = 0;
      return result(OnlineMusicAction::PlaybackReady, true);
    case OnlineMusicEvent::Kind::PlaybackCompleted:
      if (state_ != OnlineMusicState::Playing) {
        return result(OnlineMusicAction::None, true);
      }
      state_ = OnlineMusicState::WaitingNextSong;
      paused_ = false;
      next_song_started_ms_ = event.timestamp_ms;
      return result(OnlineMusicAction::PlaybackCompleted, true);
    case OnlineMusicEvent::Kind::PlaybackFailed:
      if (state_ != OnlineMusicState::Resolving &&
          state_ != OnlineMusicState::Playing) {
        return result(OnlineMusicAction::None, true);
      }
      state_ = OnlineMusicState::RetryWindow;
      retry_started_ms_ = event.timestamp_ms;
      return result(OnlineMusicAction::PlaybackFailed, true);
    case OnlineMusicEvent::Kind::Tick: {
      if (encoder_click_count_ == 1U &&
          static_cast<std::uint32_t>(event.timestamp_ms -
                                     last_encoder_press_ms_) >
              kDoubleClickWindowMs) {
        encoder_click_count_ = 0U;
        if (state_ == OnlineMusicState::Playing) {
          paused_ = !paused_;
          if (session_active_) {
            session_deadline_ms_ = event.timestamp_ms + kSessionWindowMs;
          }
          return result(OnlineMusicAction::TogglePause, true);
        }
      }
      if (state_ == OnlineMusicState::Listening &&
          static_cast<std::uint32_t>(event.timestamp_ms -
                                     listening_started_ms_) >=
              kListeningWindowMs) {
        auto timeout = stop_listening();
        timeout.listening_deadline_reached = true;
        return timeout;
      }
      if (state_ == OnlineMusicState::RetryWindow &&
          static_cast<std::uint32_t>(event.timestamp_ms - retry_started_ms_) >=
              kRetryWindowMs) {
        auto timeout = exit_mode();
        timeout.retry_deadline_reached = true;
        return timeout;
      }
      if (session_active_ &&
          static_cast<std::uint32_t>(event.timestamp_ms -
                                     session_deadline_ms_) < 0x80000000U) {
        auto timeout = exit_mode();
        timeout.session_deadline_reached = true;
        timeout.action = OnlineMusicAction::SessionExpired;
        return timeout;
      }
      if (state_ == OnlineMusicState::WaitingNextSong &&
          static_cast<std::uint32_t>(event.timestamp_ms -
                                     next_song_started_ms_) >=
              kNextSongWindowMs) {
        state_ = OnlineMusicState::Resolving;
        auto next = result(OnlineMusicAction::AutoPlayNext, true);
        next.next_song_deadline_reached = true;
        return next;
      }
      return result(OnlineMusicAction::None, true);
    }
    default:
      return result(OnlineMusicAction::None, true);
  }
}

bool OnlineMusicModeController::next_deadline_ms(
    std::uint32_t* deadline_ms) const {
  if (deadline_ms == nullptr) {
    return false;
  }
  bool available = false;
  std::uint32_t selected = 0U;
  const auto choose = [&available, &selected](std::uint32_t candidate) {
    if (!available || static_cast<std::uint32_t>(selected - candidate) <
                          0x80000000U) {
      available = true;
      selected = candidate;
    }
  };
  if (state_ == OnlineMusicState::Listening) {
    choose(listening_started_ms_ + kListeningWindowMs);
  }
  if (state_ == OnlineMusicState::RetryWindow) {
    choose(retry_started_ms_ + kRetryWindowMs);
  }
  if (state_ == OnlineMusicState::WaitingNextSong) {
    choose(next_song_started_ms_ + kNextSongWindowMs);
  }
  if (session_active_) {
    choose(session_deadline_ms_);
  }
  if (!available) {
    return false;
  }
  *deadline_ms = selected;
  return true;
}

}  // namespace ai_keyboard
