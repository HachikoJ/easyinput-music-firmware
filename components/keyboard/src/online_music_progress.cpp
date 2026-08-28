#include "keyboard/online_music_progress.h"

#include <algorithm>

namespace ai_keyboard {
namespace {

std::size_t stage_index(OnlineMusicStage stage) {
  return static_cast<std::size_t>(stage);
}

}  // namespace

void OnlineMusicProgress::reset() {
  stages_.fill(OnlineMusicStageState::Pending);
  active_ = false;
  failure_started_ms_ = 0U;
  failure_flash_active_ = false;
  all_complete_ms_ = 0U;
}

void OnlineMusicProgress::prepare(OnlineMusicStage stage) {
  start(stage);
  const auto current = stage_index(stage);
  if (current < stages_.size()) {
    stages_[current] = OnlineMusicStageState::Preparing;
  }
}

void OnlineMusicProgress::start(OnlineMusicStage stage) {
  stages_.fill(OnlineMusicStageState::Pending);
  const auto current = stage_index(stage);
  for (std::size_t index = 0; index < current; ++index) {
    stages_[index] = OnlineMusicStageState::Complete;
  }
  if (current < stages_.size()) {
    stages_[current] = OnlineMusicStageState::Active;
  }
  active_ = true;
  failure_started_ms_ = 0U;
  failure_flash_active_ = false;
  all_complete_ms_ = 0U;
}

void OnlineMusicProgress::complete(OnlineMusicStage stage,
                                   std::uint32_t now_ms) {
  const auto completed = stage_index(stage);
  if (!active_ || completed >= stages_.size()) {
    return;
  }
  for (std::size_t index = 0; index <= completed; ++index) {
    stages_[index] = OnlineMusicStageState::Complete;
  }
  if (completed + 1U < stages_.size()) {
    stages_[completed + 1U] = OnlineMusicStageState::Active;
  } else {
    all_complete_ms_ = now_ms;
  }
  failure_started_ms_ = 0U;
  failure_flash_active_ = false;
}

void OnlineMusicProgress::fail(OnlineMusicStage stage, std::uint32_t now_ms) {
  const auto failed = stage_index(stage);
  if (!active_ || failed >= stages_.size()) {
    return;
  }
  for (std::size_t index = 0; index < failed; ++index) {
    stages_[index] = OnlineMusicStageState::Complete;
  }
  stages_[failed] = OnlineMusicStageState::Failed;
  for (std::size_t index = failed + 1U; index < stages_.size(); ++index) {
    stages_[index] = OnlineMusicStageState::Pending;
  }
  failure_started_ms_ = now_ms;
  failure_flash_active_ = true;
  all_complete_ms_ = 0U;
}

void OnlineMusicProgress::update(std::uint32_t now_ms) {
  if (!active_ || !failure_flash_active_ ||
      static_cast<std::uint32_t>(now_ms - failure_started_ms_) <
          kFailureFlashMs) {
    return;
  }
  for (auto& state : stages_) {
    if (state == OnlineMusicStageState::Failed) {
      state = OnlineMusicStageState::RetryWaiting;
    }
  }
  failure_started_ms_ = 0U;
  failure_flash_active_ = false;
}

bool OnlineMusicProgress::has_failure() const {
  return std::any_of(stages_.begin(), stages_.end(), [](auto state) {
    return state == OnlineMusicStageState::Failed ||
           state == OnlineMusicStageState::RetryWaiting;
  });
}

bool OnlineMusicProgress::all_complete() const {
  return active_ &&
         std::all_of(stages_.begin(), stages_.end(), [](auto state) {
           return state == OnlineMusicStageState::Complete;
         });
}

bool OnlineMusicProgress::completion_hold_elapsed(std::uint32_t now_ms) const {
  return all_complete() &&
         static_cast<std::uint32_t>(now_ms - all_complete_ms_) >=
             kAllCompleteHoldMs;
}

}  // namespace ai_keyboard
