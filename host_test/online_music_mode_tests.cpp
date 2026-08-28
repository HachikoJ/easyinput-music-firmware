#include <cassert>

#include "keyboard/online_music_mode.h"

using ai_keyboard::OnlineMusicAction;
using ai_keyboard::OnlineMusicEvent;
using ai_keyboard::OnlineMusicModeController;
using ai_keyboard::OnlineMusicState;

OnlineMusicEvent event(OnlineMusicEvent::Kind kind, std::uint32_t now,
                       int step = 0) {
  return {kind, step, now};
}

int main() {
  OnlineMusicModeController controller;

  auto result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderPress, 100));
  assert(result.action == OnlineMusicAction::None);
  assert(!result.consumed);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderRelease, 110));
  assert(!result.consumed);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderPress, 550));
  assert(result.action == OnlineMusicAction::EnterMode);
  assert(result.state == OnlineMusicState::WaitingForCommand);
  assert(result.volume_percent == 15);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderRelease, 560));
  assert(result.consumed);

  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 1000));
  assert(result.action == OnlineMusicAction::StartListening);
  assert(result.state == OnlineMusicState::Preparing);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 20000));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::CaptureStarted, 5000));
  assert(result.action == OnlineMusicAction::ListeningReady);
  assert(result.state == OnlineMusicState::Listening);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 14999));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 15000));
  assert(result.action == OnlineMusicAction::StartResolving);
  assert(result.listening_deadline_reached);
  assert(result.state == OnlineMusicState::Resolving);

  auto recognized = event(OnlineMusicEvent::Kind::RecognitionSucceeded, 15500);
  recognized.recognized_text = "song title";
  result = controller.handle(recognized);
  assert(result.action == OnlineMusicAction::StartPlayback);
  assert(result.state == OnlineMusicState::Resolving);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::PlaybackSucceeded, 16000));
  assert(result.action == OnlineMusicAction::PlaybackReady);
  assert(result.state == OnlineMusicState::Playing);

  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderTurn, 16100, -1));
  assert(result.action == OnlineMusicAction::VolumeChanged);
  assert(result.volume_percent == 10);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderPress, 16200));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::EncoderRelease, 16210));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 16800));
  assert(result.action == OnlineMusicAction::TogglePause);
  assert(result.paused);

  result = controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 17000));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::EncoderRelease, 17010));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 17500));
  assert(result.action == OnlineMusicAction::ExitMode);
  assert(result.state == OnlineMusicState::Normal);

  controller.reset();
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 18000));
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 18400));
  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 19000));
  assert(result.state == OnlineMusicState::Preparing);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::RecognitionFailed, 20000));
  assert(result.action == OnlineMusicAction::FailureBeep3);
  assert(result.state == OnlineMusicState::RetryWindow);

  // A failure emits the three-beep action, then allows another 10 second
  // listening attempt before the 30 second idle timeout exits the mode.
  controller.reset();
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 20000));
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 20400));
  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 21000));
  assert(result.action == OnlineMusicAction::StartListening);
  result = controller.handle(event(OnlineMusicEvent::Kind::CaptureStarted, 21100));
  assert(result.action == OnlineMusicAction::ListeningReady);
  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 21500));
  assert(result.action == OnlineMusicAction::StartResolving);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::RecognitionFailed, 22000));
  assert(result.action == OnlineMusicAction::FailureBeep3);
  assert(result.state == OnlineMusicState::RetryWindow);
  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 23000));
  assert(result.action == OnlineMusicAction::StartListening);
  controller.handle(event(OnlineMusicEvent::Kind::CaptureStarted, 23100));
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 53000));
  assert(result.action == OnlineMusicAction::StartResolving);
  result = controller.handle(event(OnlineMusicEvent::Kind::RecognitionFailed, 54000));
  assert(result.action == OnlineMusicAction::FailureBeep3);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 84000));
  assert(result.action == OnlineMusicAction::ExitMode);
  assert(result.retry_deadline_reached);
  assert(result.state == OnlineMusicState::Normal);

  controller.reset();
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 90000));
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 90400));
  controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 91000));
  controller.handle(event(OnlineMusicEvent::Kind::CaptureStarted, 91100));
  controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 91500));
  auto playback = event(OnlineMusicEvent::Kind::RecognitionSucceeded, 92000);
  playback.recognized_text = "song";
  controller.handle(playback);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::PlaybackSucceeded, 92500));
  assert(result.state == OnlineMusicState::Playing);
  result = controller.handle(
      event(OnlineMusicEvent::Kind::PlaybackFailed, 93000));
  assert(result.action == OnlineMusicAction::PlaybackFailed);
  assert(result.state == OnlineMusicState::RetryWindow);

  // A natural EOF is distinct from a playback failure: remain in online mode
  // and wait 30 seconds before requesting a similar next result.
  controller.reset();
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 100000));
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 100400));
  controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 101000));
  controller.handle(event(OnlineMusicEvent::Kind::CaptureStarted, 101100));
  std::uint32_t deadline = 0U;
  assert(controller.next_deadline_ms(&deadline));
  assert(deadline == 101100U + OnlineMusicModeController::kListeningWindowMs);
  controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 101500));
  auto natural_query = event(OnlineMusicEvent::Kind::RecognitionSucceeded,
                             102000);
  natural_query.recognized_text = "song";
  controller.handle(natural_query);
  controller.handle(event(OnlineMusicEvent::Kind::PlaybackSucceeded, 102500));
  result = controller.handle(
      event(OnlineMusicEvent::Kind::PlaybackCompleted, 103000));
  assert(result.action == OnlineMusicAction::PlaybackCompleted);
  assert(result.state == OnlineMusicState::WaitingNextSong);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 132999));
  assert(result.action == OnlineMusicAction::None);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick, 133000));
  assert(result.action == OnlineMusicAction::AutoPlayNext);
  assert(result.next_song_deadline_reached);
  assert(result.state == OnlineMusicState::Resolving);

  // User input starts a fresh listening attempt and refreshes the session
  // deadline; automatic continuation above does not itself refresh it.
  controller.reset();
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 200000));
  controller.handle(event(OnlineMusicEvent::Kind::EncoderPress, 200400));
  result = controller.handle(event(OnlineMusicEvent::Kind::KeyPress, 200500));
  assert(result.action == OnlineMusicAction::StartListening);
  result = controller.handle(event(OnlineMusicEvent::Kind::Tick,
                                   200500 + OnlineMusicModeController::kSessionWindowMs));
  assert(result.action == OnlineMusicAction::SessionExpired);
  assert(result.session_deadline_reached);
  assert(result.state == OnlineMusicState::Normal);
  return 0;
}
