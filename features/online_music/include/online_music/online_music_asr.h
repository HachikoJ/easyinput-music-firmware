#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace easy_input::online_music {

// The protocol helper deliberately does not own a socket. The application
// task can use these messages with esp_transport_ws (or another WebSocket
// implementation) while keeping credentials and transport lifecycle outside
// the JSON parser.
enum class AsrRegion : std::uint8_t {
  ChinaBeijing,
  Singapore,
};

struct OnlineMusicAsrConfig {
  std::string api_key;
  std::string workspace_id;
  AsrRegion region = AsrRegion::ChinaBeijing;
  std::string model = "qwen-audio-3.0-asr-flash-streaming";
  std::string language = "zh";
};

enum class OnlineMusicAsrEvent : std::uint8_t {
  Invalid,
  TaskStarted,
  ResultGenerated,
  TaskFinished,
  TaskFailed,
};

struct OnlineMusicAsrParsedEvent {
  OnlineMusicAsrEvent event = OnlineMusicAsrEvent::Invalid;
  std::string task_id;
  std::string transcript;
  std::string error_code;
  std::string error_message;
  bool sentence_begin = false;
  bool sentence_end = false;
  bool heartbeat = false;
};

using PcmChunkSink = bool (*)(void* context, const std::uint8_t* pcm,
                              std::size_t bytes);

class PcmChunker {
 public:
  static constexpr std::size_t kChunkBytes = 3200U;

  bool append(const std::uint8_t* pcm, std::size_t bytes,
              PcmChunkSink sink, void* context);
  bool finish(PcmChunkSink sink, void* context);
  void reset();

 private:
  std::array<std::uint8_t, kChunkBytes> chunk_{};
  std::size_t used_ = 0U;
};

class OnlineMusicAsr {
 public:
  static constexpr std::size_t kMaxApiKeyBytes = 256U;
  static constexpr std::size_t kMaxWorkspaceIdBytes = 128U;
  static constexpr std::size_t kMaxTaskIdBytes = 64U;
  static constexpr std::size_t kMaxTranscriptBytes = 512U;

  // Builds the current workspace-specific endpoint. No network access occurs.
  static bool build_endpoint(const OnlineMusicAsrConfig& config,
                             std::string* endpoint);

  // Returns an HTTP header value for the WebSocket handshake. The caller must
  // keep the returned string private and must not log it.
  static bool build_authorization_header(const OnlineMusicAsrConfig& config,
                                         std::string* header_value);

  // Builds the run-task and finish-task text frames required by the official
  // Qwen-Audio-3.0-ASR-Flash-Streaming protocol.
  static bool build_run_task(const OnlineMusicAsrConfig& config,
                             const std::string& task_id,
                             std::string* frame);
  static bool build_finish_task(const std::string& task_id,
                                std::string* frame);

  // Parses one server text frame. Binary frames are audio input from the
  // client and should not be passed here.
  static bool parse_server_event(const std::string& frame,
                                 OnlineMusicAsrParsedEvent* parsed);

  bool configure(const OnlineMusicAsrConfig& config);
  bool configured() const { return configured_; }
  bool task_active() const { return task_active_; }
  const OnlineMusicAsrConfig& config() const { return config_; }
  const std::string& transcript() const { return transcript_; }
  bool transcript_final() const { return transcript_final_; }

  bool start(const std::string& task_id, std::string* run_frame);
  bool append_audio(const std::uint8_t* pcm, std::size_t bytes) const;
  bool finish(std::string* finish_frame);
  bool ingest_server_event(const std::string& frame,
                           OnlineMusicAsrParsedEvent* parsed);

 private:
  OnlineMusicAsrConfig config_;
  bool configured_ = false;
  bool task_active_ = false;
  bool task_finishing_ = false;
  std::string task_id_;
  std::string transcript_;
  bool transcript_final_ = false;
};

}  // namespace easy_input::online_music
