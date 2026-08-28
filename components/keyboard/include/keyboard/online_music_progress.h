#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ai_keyboard {

enum class OnlineMusicStage : std::uint8_t {
  Recording,
  Recognizing,
  Searching,
  ResolvingUrl,
  StartingPlayback,
  Count,
};

enum class OnlineMusicStageState : std::uint8_t {
  Pending,
  Preparing,
  Active,
  Complete,
  Failed,
  RetryWaiting,
};

class OnlineMusicProgress {
 public:
  static constexpr std::size_t kStageCount =
      static_cast<std::size_t>(OnlineMusicStage::Count);
  static constexpr std::uint32_t kFailureFlashMs = 1500U;
  static constexpr std::uint32_t kAllCompleteHoldMs = 350U;

  void reset();
  void prepare(OnlineMusicStage stage);
  void start(OnlineMusicStage stage);
  void complete(OnlineMusicStage stage, std::uint32_t now_ms = 0U);
  void fail(OnlineMusicStage stage, std::uint32_t now_ms);
  void update(std::uint32_t now_ms);

  bool active() const { return active_; }
  bool has_failure() const;
  bool all_complete() const;
  bool completion_hold_elapsed(std::uint32_t now_ms) const;
  const std::array<OnlineMusicStageState, kStageCount>& stages() const {
    return stages_;
  }

 private:
  std::array<OnlineMusicStageState, kStageCount> stages_{};
  bool active_ = false;
  std::uint32_t failure_started_ms_ = 0U;
  bool failure_flash_active_ = false;
  std::uint32_t all_complete_ms_ = 0U;
};

}  // namespace ai_keyboard
