#include <cassert>

#include "keyboard/music_mode.h"

using ai_keyboard::InputId;
using ai_keyboard::InputPhase;
using ai_keyboard::MusicModeAction;
using ai_keyboard::MusicModeController;
using ai_keyboard::MusicModeEvent;
using ai_keyboard::MusicModeState;

MusicModeEvent press(InputId input, std::uint32_t timestamp) {
  return {input, InputPhase::Pressed, 0, timestamp};
}

MusicModeEvent release(InputId input, std::uint32_t timestamp) {
  return {input, InputPhase::Released, 0, timestamp};
}

MusicModeEvent turn(InputId input, int step, std::uint32_t timestamp) {
  return {input, InputPhase::Pressed, step, timestamp};
}

int main() {
  MusicModeController controller;

  // A click outside the 250 ms window remains available to the normal input
  // dispatcher and starts a fresh double-click candidate.
  auto result = controller.handle(press(InputId::EncoderPress, 100));
  assert(result.action == MusicModeAction::None);
  assert(!result.consumed);
  result = controller.handle(release(InputId::EncoderPress, 110));
  assert(!result.consumed);
  result = controller.handle(press(InputId::EncoderPress, 351));
  assert(result.action == MusicModeAction::None);
  assert(result.state == MusicModeState::Normal);
  assert(!result.consumed);
  controller.handle(release(InputId::EncoderPress, 360));

  // Exactly 250 ms is inside the music gesture window.
  result = controller.handle(press(InputId::EncoderPress, 601));
  assert(result.action == MusicModeAction::EnterMusic);
  assert(result.state == MusicModeState::MusicPlayback);
  assert(result.volume_percent == 15);
  assert(result.consumed);
  result = controller.handle(release(InputId::EncoderPress, 610));
  assert(result.action == MusicModeAction::None);
  assert(result.consumed);

  // Music mode owns every S1-S8 edge. Every press uses the same ordered
  // playlist action; the platform chooses track zero first and advances after.
  for (const auto key : {InputId::Key1, InputId::Key2, InputId::Key3,
                         InputId::Key4,
                         InputId::Key5, InputId::Key6, InputId::Key7,
                         InputId::Key8}) {
    result = controller.handle(press(key, 700));
    assert(result.action == MusicModeAction::PlayOrNext);
    assert(result.state == MusicModeState::MusicPlayback);
    assert(result.consumed);
    assert(!controller.paused());
    result = controller.handle(release(key, 710));
    assert(result.action == MusicModeAction::None);
    assert(result.consumed);
  }

  // The V2 board reports physical clockwise/right rotation as a negative step,
  // matching the existing LED brightness control. Music volume follows the
  // same direction and changes five percent per detent.
  result = controller.handle(turn(InputId::EncoderRight, 1, 800));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 10);
  assert(result.consumed);
  result = controller.handle(turn(InputId::EncoderLeft, -1, 810));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 15);
  assert(result.consumed);

  result = controller.handle(turn(InputId::EncoderLeft, -2, 820));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 25);
  result = controller.handle(turn(InputId::EncoderRight, 3, 830));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 10);

  result = controller.handle(turn(InputId::EncoderRight, 100, 840));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 0);
  result = controller.handle(turn(InputId::EncoderRight, 1, 850));
  assert(result.action == MusicModeAction::None);
  assert(result.volume_percent == 0);
  assert(result.consumed);
  result = controller.handle(turn(InputId::EncoderLeft, -100, 860));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 100);
  result = controller.handle(turn(InputId::EncoderLeft, -1, 870));
  assert(result.action == MusicModeAction::None);
  assert(result.volume_percent == 100);
  assert(result.consumed);
  result = controller.handle(turn(InputId::EncoderLeft, 0, 880));
  assert(result.action == MusicModeAction::None);
  assert(result.volume_percent == 100);
  assert(result.consumed);

  result = controller.handle(press(InputId::EncoderPress, 1000));
  assert(result.action == MusicModeAction::None);
  assert(result.consumed);
  result = controller.handle(release(InputId::EncoderPress, 1010));
  assert(result.action == MusicModeAction::TogglePause);
  assert(result.consumed);
  assert(controller.paused());
  result = controller.handle(turn(InputId::EncoderRight, 2, 1020));
  assert(result.action == MusicModeAction::VolumeChanged);
  assert(result.volume_percent == 90);
  assert(controller.paused());
  result = controller.handle(press(InputId::EncoderPress, 1300));
  assert(result.action == MusicModeAction::None);
  assert(result.consumed);
  result = controller.handle(release(InputId::EncoderPress, 1310));
  assert(result.action == MusicModeAction::TogglePause);
  assert(result.consumed);
  assert(!controller.paused());

  result = controller.handle(press(InputId::Key1, 1400));
  assert(result.action == MusicModeAction::PlayOrNext);
  assert(result.volume_percent == 90);

  // The exit gesture and its trailing release are consumed even though the
  // controller has already returned to normal state.
  controller.handle(press(InputId::EncoderPress, 1600));
  result = controller.handle(release(InputId::EncoderPress, 1610));
  assert(result.action == MusicModeAction::TogglePause);
  assert(result.consumed);
  result = controller.handle(press(InputId::EncoderPress, 1700));
  assert(result.action == MusicModeAction::ExitMusic);
  assert(result.state == MusicModeState::Normal);
  assert(result.consumed);
  result = controller.handle(release(InputId::EncoderPress, 1710));
  assert(result.action == MusicModeAction::None);
  assert(result.state == MusicModeState::Normal);
  assert(result.consumed);
  assert(result.volume_percent == 90);

  // After the exit gesture is complete, normal keys and encoder rotation are
  // no longer owned by the music controller.
  result = controller.handle(press(InputId::Key1, 1800));
  assert(result.action == MusicModeAction::None);
  assert(result.state == MusicModeState::Normal);
  assert(!result.consumed);
  result = controller.handle(press(InputId::Key8, 1810));
  assert(result.action == MusicModeAction::None);
  assert(result.state == MusicModeState::Normal);
  assert(!result.consumed);
  result = controller.handle(turn(InputId::EncoderRight, 1, 1820));
  assert(result.action == MusicModeAction::None);
  assert(!result.consumed);
  assert(result.volume_percent == 90);

  // A new music session always starts from the 15% default.
  controller.handle(press(InputId::EncoderPress, 1900));
  controller.handle(release(InputId::EncoderPress, 1910));
  result = controller.handle(press(InputId::EncoderPress, 2000));
  assert(result.action == MusicModeAction::EnterMusic);
  assert(result.volume_percent == 15);

  controller.reset();
  assert(controller.state() == MusicModeState::Normal);
  assert(controller.volume_percent() == 15);
  assert(!controller.paused());
  return 0;
}
