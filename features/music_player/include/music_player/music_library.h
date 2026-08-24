#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_partition.h"

namespace easy_input::music_storage {

inline constexpr std::size_t kMusicTrackCount = 2U;

struct TrackView {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0U;
};

// Owns one immutable Flash mapping. A returned TrackView remains valid until
// the next track() call, close(), or destruction of this object.
class MusicLibrary {
 public:
  MusicLibrary() = default;
  ~MusicLibrary();

  MusicLibrary(const MusicLibrary&) = delete;
  MusicLibrary& operator=(const MusicLibrary&) = delete;

  [[nodiscard]] esp_err_t begin();
  [[nodiscard]] std::size_t track_count() const;
  [[nodiscard]] bool track(std::size_t index, TrackView* output) const;
  [[nodiscard]] bool ready() const;
  void close();

 private:
  struct TrackMetadata {
    std::uint32_t offset = 0U;
    std::uint32_t length = 0U;
    std::array<std::uint8_t, 32> sha256{};
  };

  void release_mapping() const;
  [[nodiscard]] bool descriptor_is_valid(
      const esp_partition_t* partition) const;

  const esp_partition_t* partition_ = nullptr;
  std::array<TrackMetadata, kMusicTrackCount> tracks_{};
  mutable const std::uint8_t* mapped_data_ = nullptr;
  mutable std::size_t mapped_size_ = 0U;
  mutable esp_partition_mmap_handle_t mapping_handle_ = 0U;
  bool ready_ = false;
};

}  // namespace easy_input::music_storage
