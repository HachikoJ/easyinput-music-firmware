#include "online_music/ws_text_assembler.h"

#include <utility>

namespace easy_input::online_music {

void WsTextAssembler::reset() {
  message_.clear();
  frame_bytes_ = 0U;
  frame_received_ = 0U;
  frame_opcode_ = 0U;
  frame_fin_ = false;
  frame_active_ = false;
  message_active_ = false;
}

WsTextAssemblyResult WsTextAssembler::append(
    std::uint8_t opcode,
    bool fin,
    std::size_t payload_bytes,
    const char* data,
    std::size_t bytes,
    std::string* message) {
  if (message == nullptr || data == nullptr || bytes == 0U) {
    reset();
    return WsTextAssemblyResult::ProtocolError;
  }
  if (!frame_active_) {
    if (payload_bytes == 0U || payload_bytes > kMaxMessageBytes ||
        message_.size() + payload_bytes > kMaxMessageBytes) {
      reset();
      return WsTextAssemblyResult::TooLarge;
    }
    if ((opcode == kTextOpcode && message_active_) ||
        (opcode == kContinuationOpcode && !message_active_) ||
        (opcode != kTextOpcode && opcode != kContinuationOpcode)) {
      reset();
      return WsTextAssemblyResult::ProtocolError;
    }
    if (opcode == kTextOpcode) {
      message_.clear();
      message_active_ = true;
    }
    frame_bytes_ = payload_bytes;
    frame_received_ = 0U;
    frame_opcode_ = opcode;
    frame_fin_ = fin;
    frame_active_ = true;
  } else if (opcode != frame_opcode_ || fin != frame_fin_ ||
             payload_bytes != frame_bytes_) {
    reset();
    return WsTextAssemblyResult::ProtocolError;
  }
  if (bytes > frame_bytes_ - frame_received_) {
    reset();
    return WsTextAssemblyResult::ProtocolError;
  }
  message_.append(data, bytes);
  frame_received_ += bytes;
  if (frame_received_ != frame_bytes_) {
    return WsTextAssemblyResult::NeedMore;
  }
  frame_active_ = false;
  if (!frame_fin_) {
    return WsTextAssemblyResult::NeedMore;
  }
  *message = std::move(message_);
  reset();
  return WsTextAssemblyResult::MessageReady;
}

}  // namespace easy_input::online_music
