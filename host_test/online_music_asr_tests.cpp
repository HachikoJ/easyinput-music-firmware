#include <cassert>
#include <cstdint>
#include <vector>
#include <string>

#include "online_music/online_music_asr.h"

using easy_input::online_music::AsrRegion;
using easy_input::online_music::OnlineMusicAsr;
using easy_input::online_music::OnlineMusicAsrConfig;
using easy_input::online_music::OnlineMusicAsrEvent;
using easy_input::online_music::PcmChunker;

bool collect_pcm(void* context, const std::uint8_t* pcm, std::size_t bytes) {
  auto* chunks = static_cast<std::vector<std::vector<std::uint8_t>>*>(context);
  chunks->emplace_back(pcm, pcm + bytes);
  return true;
}

int main() {
  OnlineMusicAsrConfig config;
  config.api_key = "test-key";
  config.workspace_id = "ws-demo";
  config.region = AsrRegion::ChinaBeijing;
  std::string value;
  assert(OnlineMusicAsr::build_endpoint(config, &value));
  assert(value == "wss://ws-demo.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference");
  assert(OnlineMusicAsr::build_authorization_header(config, &value));
  assert(value == "Bearer test-key");
  OnlineMusicAsr asr;
  assert(asr.configure(config));
  assert(asr.start("task-1", &value));
  assert(value.find("run-task") != std::string::npos);
  const std::string started =
      R"({"header":{"task_id":"task-1","event":"task-started","attributes":{}},"payload":{}})";
  easy_input::online_music::OnlineMusicAsrParsedEvent parsed;
  assert(asr.ingest_server_event(started, &parsed));
  assert(parsed.event == OnlineMusicAsrEvent::TaskStarted);
  const std::string result =
      R"({"header":{"task_id":"task-1","event":"result-generated","attributes":{}},"payload":{"output":{"sentence":{"text":"周杰伦晴天","sentence_begin":true,"sentence_end":true,"heartbeat":false}}}})";
  assert(asr.ingest_server_event(result, &parsed));
  assert(parsed.transcript == "周杰伦晴天");
  assert(parsed.sentence_end);
  assert(asr.transcript() == "周杰伦晴天");
  assert(asr.transcript_final());
  const std::string later_partial =
      R"({"header":{"task_id":"task-1","event":"result-generated"},"payload":{"output":{"sentence":{"text":"噪声","sentence_end":false,"heartbeat":false}}}})";
  assert(asr.ingest_server_event(later_partial, &parsed));
  assert(asr.transcript() == "周杰伦晴天");
  const std::string heartbeat =
      R"({"header":{"task_id":"task-1","event":"result-generated"},"payload":{"output":{"sentence":{"heartbeat":true}}}})";
  assert(asr.ingest_server_event(heartbeat, &parsed));
  assert(parsed.transcript.empty());
  assert(asr.transcript() == "周杰伦晴天");
  std::uint8_t pcm[3200] = {};
  assert(asr.append_audio(pcm, sizeof(pcm)));
  assert(asr.finish(&value));
  assert(!asr.task_active());
  assert(value.find("finish-task") != std::string::npos);

  PcmChunker chunker;
  std::vector<std::vector<std::uint8_t>> chunks;
  std::uint8_t frame[640] = {};
  for (std::size_t index = 0U; index < 4U; ++index) {
    frame[0] = static_cast<std::uint8_t>(index + 1U);
    assert(chunker.append(frame, sizeof(frame), collect_pcm, &chunks));
  }
  assert(chunks.empty());
  assert(chunker.finish(collect_pcm, &chunks));
  assert(chunks.size() == 1U);
  assert(chunks[0].size() == 2560U);
  assert(chunks[0][0] == 1U);
  assert(chunks[0][640] == 2U);
  assert(chunks[0][1280] == 3U);
  assert(chunks[0][1920] == 4U);

  chunks.clear();
  for (std::size_t index = 0U; index < 5U; ++index) {
    assert(chunker.append(frame, sizeof(frame), collect_pcm, &chunks));
  }
  assert(chunks.size() == 1U && chunks[0].size() == 3200U);
  assert(chunker.finish(collect_pcm, &chunks));
  assert(chunks.size() == 1U);
  return 0;
}
