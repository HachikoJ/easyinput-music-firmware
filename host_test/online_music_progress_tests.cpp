#include <cassert>

#include "keyboard/online_music_progress.h"

using ai_keyboard::OnlineMusicProgress;
using ai_keyboard::OnlineMusicStage;
using ai_keyboard::OnlineMusicStageState;

int main() {
  OnlineMusicProgress progress;
  progress.prepare(OnlineMusicStage::Recording);
  assert(progress.stages()[0] == OnlineMusicStageState::Preparing);
  assert(progress.stages()[1] == OnlineMusicStageState::Pending);

  progress.start(OnlineMusicStage::Recording);
  assert(progress.stages()[0] == OnlineMusicStageState::Active);
  assert(progress.stages()[1] == OnlineMusicStageState::Pending);

  progress.complete(OnlineMusicStage::Recording, 100U);
  assert(progress.stages()[0] == OnlineMusicStageState::Complete);
  assert(progress.stages()[1] == OnlineMusicStageState::Active);
  progress.complete(OnlineMusicStage::Recognizing, 200U);
  progress.complete(OnlineMusicStage::Searching, 300U);
  assert(progress.stages()[2] == OnlineMusicStageState::Complete);
  assert(progress.stages()[3] == OnlineMusicStageState::Active);

  progress.fail(OnlineMusicStage::ResolvingUrl, 400U);
  assert(progress.stages()[0] == OnlineMusicStageState::Complete);
  assert(progress.stages()[3] == OnlineMusicStageState::Failed);
  assert(progress.stages()[4] == OnlineMusicStageState::Pending);
  progress.update(400U + OnlineMusicProgress::kFailureFlashMs - 1U);
  assert(progress.stages()[3] == OnlineMusicStageState::Failed);
  progress.update(400U + OnlineMusicProgress::kFailureFlashMs);
  assert(progress.stages()[3] == OnlineMusicStageState::RetryWaiting);

  for (std::size_t failed = 0U; failed < OnlineMusicProgress::kStageCount;
       ++failed) {
    progress.start(static_cast<OnlineMusicStage>(failed));
    progress.fail(static_cast<OnlineMusicStage>(failed), 500U);
    for (std::size_t index = 0U; index < OnlineMusicProgress::kStageCount;
         ++index) {
      const auto expected = index < failed
                                ? OnlineMusicStageState::Complete
                                : index == failed
                                      ? OnlineMusicStageState::Failed
                                      : OnlineMusicStageState::Pending;
      assert(progress.stages()[index] == expected);
    }
    progress.update(500U + OnlineMusicProgress::kFailureFlashMs);
    assert(progress.stages()[failed] ==
           OnlineMusicStageState::RetryWaiting);
  }

  progress.start(OnlineMusicStage::Recording);
  assert(progress.stages()[0] == OnlineMusicStageState::Active);
  for (std::size_t index = 1U; index < OnlineMusicProgress::kStageCount;
       ++index) {
    assert(progress.stages()[index] == OnlineMusicStageState::Pending);
  }

  progress.complete(OnlineMusicStage::Recording, 1000U);
  progress.complete(OnlineMusicStage::Recognizing, 1100U);
  progress.complete(OnlineMusicStage::Searching, 1200U);
  progress.complete(OnlineMusicStage::ResolvingUrl, 1300U);
  progress.complete(OnlineMusicStage::StartingPlayback, 1400U);
  assert(progress.all_complete());
  assert(!progress.completion_hold_elapsed(1749U));
  assert(progress.completion_hold_elapsed(1750U));

  progress.reset();
  assert(!progress.active());
  for (const auto state : progress.stages()) {
    assert(state == OnlineMusicStageState::Pending);
  }
  return 0;
}
