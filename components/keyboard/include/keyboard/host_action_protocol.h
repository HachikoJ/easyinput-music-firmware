#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ai_keyboard {

inline constexpr std::uint8_t kHostActionV1ReportId = 0x11;
inline constexpr std::uint8_t kHostActionV1Kind = 0x05;
inline constexpr std::uint8_t kHostActionV1ChunkIndex = 0;
inline constexpr std::uint8_t kHostActionV1TotalChunks = 1;
inline constexpr std::size_t kHostActionV1DataLength = 36;
inline constexpr std::size_t kHostActionV1PayloadLength = 63;
inline constexpr std::size_t kHostActionV1HeaderLength = 4;

struct HostActionV1Wire {
  std::uint8_t report_id = kHostActionV1ReportId;
  std::uint8_t kind = kHostActionV1Kind;
  std::uint8_t chunk_index = kHostActionV1ChunkIndex;
  std::uint8_t total_chunks = kHostActionV1TotalChunks;
  std::uint8_t data_length = kHostActionV1DataLength;
  std::string data;
  std::array<std::uint8_t, kHostActionV1PayloadLength> payload{};
};

bool is_canonical_host_action_uuid(std::string_view uuid);
std::optional<HostActionV1Wire> encode_host_action_v1(std::string_view uuid);

}  // namespace ai_keyboard
