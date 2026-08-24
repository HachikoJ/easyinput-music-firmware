#include <array>
#include <cassert>
#include <string>

#include "keyboard/host_action_protocol.h"

void host_action_wire_uses_frozen_v1_fields() {
  const auto encoded = ai_keyboard::encode_host_action_v1(
      "123e4567-e89b-12d3-a456-426614174000");
  assert(encoded.has_value());
  assert(encoded->report_id == 0x11);
  assert(encoded->kind == 0x05);
  assert(encoded->chunk_index == 0);
  assert(encoded->total_chunks == 1);
  assert(encoded->data_length == 36);
  assert(encoded->data == "123e4567-e89b-12d3-a456-426614174000");
  assert(encoded->payload.size() == 63);
  assert(encoded->payload[0] == 0x05);
  assert(encoded->payload[1] == 0);
  assert(encoded->payload[2] == 1);
  assert(encoded->payload[3] == 36);
  assert(std::string(reinterpret_cast<const char*>(encoded->payload.data() + 4), 36) ==
         encoded->data);
  for (std::size_t index = 40; index < encoded->payload.size(); ++index) {
    assert(encoded->payload[index] == 0);
  }
}

void host_action_wire_rejects_noncanonical_uuid() {
  for (const auto& uuid : {
           std::string("123E4567-e89b-12d3-a456-426614174000"),
           std::string("123e4567e89b-12d3-a456-426614174000"),
           std::string("123e4567-e89b-12d3-a456-42661417400"),
           std::string("123e4567-e89b-12d3-a456-42661417400g")}) {
    assert(!ai_keyboard::encode_host_action_v1(uuid).has_value());
  }
}

int main() {
  host_action_wire_uses_frozen_v1_fields();
  host_action_wire_rejects_noncanonical_uuid();
  return 0;
}
