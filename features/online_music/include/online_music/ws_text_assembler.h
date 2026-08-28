#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace easy_input::online_music {

enum class WsTextAssemblyResult : std::uint8_t {
  NeedMore,
  MessageReady,
  ProtocolError,
  TooLarge,
};

class WsTextAssembler {
 public:
  static constexpr std::size_t kMaxMessageBytes = 32U * 1024U;
  static constexpr std::uint8_t kContinuationOpcode = 0x00U;
  static constexpr std::uint8_t kTextOpcode = 0x01U;

  WsTextAssemblyResult append(std::uint8_t opcode,
                              bool fin,
                              std::size_t payload_bytes,
                              const char* data,
                              std::size_t bytes,
                              std::string* message);
  void reset();

 private:
  std::string message_;
  std::size_t frame_bytes_ = 0U;
  std::size_t frame_received_ = 0U;
  std::uint8_t frame_opcode_ = 0U;
  bool frame_fin_ = false;
  bool frame_active_ = false;
  bool message_active_ = false;
};

}  // namespace easy_input::online_music
