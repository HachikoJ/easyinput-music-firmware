#include "keyboard/host_action_protocol.h"

namespace ai_keyboard {

bool is_canonical_host_action_uuid(std::string_view uuid) {
  if (uuid.size() != kHostActionV1DataLength) return false;
  for (std::size_t i = 0; i < uuid.size(); ++i) {
    const bool hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    if (hyphen) {
      if (uuid[i] != '-') return false;
      continue;
    }
    const char ch = uuid[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  }
  return true;
}

std::optional<HostActionV1Wire> encode_host_action_v1(std::string_view uuid) {
  if (!is_canonical_host_action_uuid(uuid)) return std::nullopt;
  HostActionV1Wire wire;
  wire.data.assign(uuid);
  wire.payload[0] = wire.kind;
  wire.payload[1] = wire.chunk_index;
  wire.payload[2] = wire.total_chunks;
  wire.payload[3] = wire.data_length;
  for (std::size_t i = 0; i < uuid.size(); ++i) {
    wire.payload[kHostActionV1HeaderLength + i] =
        static_cast<std::uint8_t>(uuid[i]);
  }
  return wire;
}

}  // namespace ai_keyboard
