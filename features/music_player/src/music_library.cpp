#include "music_player/music_library.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace easy_input::music_storage {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{
    'E', 'I', 'M', 'U', 'S', 'I', 'C', 0,
}};
constexpr std::uint32_t kVersion = 1U;
constexpr std::uint32_t kHeaderSize = 0x1000U;
constexpr std::uint32_t kPartitionAddress = 0x430000U;
constexpr std::uint32_t kPartitionSize = 0x400000U;
constexpr esp_partition_type_t kPartitionType =
    static_cast<esp_partition_type_t>(0x40);
constexpr esp_partition_subtype_t kPartitionSubtype =
    static_cast<esp_partition_subtype_t>(0x02);
constexpr std::size_t kFixedHeaderBytes = 24U;
constexpr std::size_t kTrackEntryBytes = 40U;
constexpr std::size_t kParsedHeaderBytes =
    kFixedHeaderBytes + kMusicTrackCount * kTrackEntryBytes;

std::uint32_t read_u32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8U) |
         (static_cast<std::uint32_t>(data[2]) << 16U) |
         (static_cast<std::uint32_t>(data[3]) << 24U);
}

bool valid_range(std::uint32_t offset, std::uint32_t length) {
  return offset >= kHeaderSize &&
         (offset % kHeaderSize) == 0U &&
         length != 0U &&
         offset <= kPartitionSize &&
         length <= kPartitionSize - offset;
}

}  // namespace

MusicLibrary::~MusicLibrary() {
  close();
}

esp_err_t MusicLibrary::begin() {
  close();
  const auto* partition = esp_partition_find_first(
      kPartitionType, kPartitionSubtype, "music");
  if (!descriptor_is_valid(partition)) {
    return partition == nullptr ? ESP_ERR_NOT_FOUND
                                : ESP_ERR_INVALID_RESPONSE;
  }

  std::array<std::uint8_t, kParsedHeaderBytes> header{};
  const auto read_result = esp_partition_read(
      partition, 0U, header.data(), header.size());
  if (read_result != ESP_OK) {
    return read_result;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) ||
      read_u32(header.data() + 8U) != kVersion ||
      read_u32(header.data() + 12U) != kHeaderSize ||
      read_u32(header.data() + 16U) != kMusicTrackCount ||
      read_u32(header.data() + 20U) != 0U) {
    return ESP_ERR_INVALID_RESPONSE;
  }

  for (std::size_t index = 0U; index < kMusicTrackCount; ++index) {
    const auto entry_offset = kFixedHeaderBytes + index * kTrackEntryBytes;
    auto& metadata = tracks_[index];
    metadata.offset = read_u32(header.data() + entry_offset);
    metadata.length = read_u32(header.data() + entry_offset + 4U);
    std::copy_n(header.data() + entry_offset + 8U,
                metadata.sha256.size(),
                metadata.sha256.begin());
    if (!valid_range(metadata.offset, metadata.length)) {
      close();
      return ESP_ERR_INVALID_RESPONSE;
    }
    if (index != 0U) {
      const auto& previous = tracks_[index - 1U];
      const auto previous_end =
          static_cast<std::uint64_t>(previous.offset) + previous.length;
      if (metadata.offset < previous_end) {
        close();
        return ESP_ERR_INVALID_RESPONSE;
      }
    }
  }

  partition_ = partition;
  ready_ = true;
  return ESP_OK;
}

std::size_t MusicLibrary::track_count() const {
  return ready_ ? kMusicTrackCount : 0U;
}

bool MusicLibrary::track(std::size_t index, TrackView* output) const {
  if (output == nullptr) {
    return false;
  }
  *output = {};
  if (!ready_ || partition_ == nullptr || index >= kMusicTrackCount) {
    return false;
  }

  release_mapping();
  const auto& metadata = tracks_[index];
  const void* mapped = nullptr;
  esp_partition_mmap_handle_t handle = 0U;
  if (esp_partition_mmap(partition_,
                         metadata.offset,
                         metadata.length,
                         ESP_PARTITION_MMAP_DATA,
                         &mapped,
                         &handle) != ESP_OK ||
      mapped == nullptr) {
    return false;
  }
  mapped_data_ = static_cast<const std::uint8_t*>(mapped);
  mapped_size_ = metadata.length;
  mapping_handle_ = handle;

  output->data = mapped_data_;
  output->size = mapped_size_;
  return true;
}

bool MusicLibrary::ready() const {
  return ready_;
}

void MusicLibrary::close() {
  release_mapping();
  partition_ = nullptr;
  tracks_ = {};
  ready_ = false;
}

void MusicLibrary::release_mapping() const {
  if (mapped_data_ != nullptr) {
    esp_partition_munmap(mapping_handle_);
  }
  mapped_data_ = nullptr;
  mapped_size_ = 0U;
  mapping_handle_ = 0U;
}

bool MusicLibrary::descriptor_is_valid(
    const esp_partition_t* partition) const {
  return partition != nullptr &&
         partition->type == kPartitionType &&
         partition->subtype == kPartitionSubtype &&
         partition->address == kPartitionAddress &&
         partition->size == kPartitionSize &&
         partition->erase_size == kHeaderSize &&
         !partition->encrypted &&
         partition->readonly &&
         std::strcmp(partition->label, "music") == 0;
}

}  // namespace easy_input::music_storage
