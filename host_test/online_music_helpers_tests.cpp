#include <cassert>
#include <string>

#include "online_music/song_query.h"
#include "online_music/ws_text_assembler.h"

using easy_input::online_music::WsTextAssembler;
using easy_input::online_music::WsTextAssemblyResult;
using easy_input::online_music::extract_song_search_query;

int main() {
  assert(extract_song_search_query("播放稻香。") == "稻香");
  assert(extract_song_search_query("帮我播放周杰伦的晴天") ==
         "周杰伦的晴天");
  assert(extract_song_search_query("来一首海阔天空") == "海阔天空");
  assert(extract_song_search_query("请帮我播放一下 稻香！") == "稻香");
  assert(extract_song_search_query("我想听一首 夜曲") == "夜曲");
  assert(extract_song_search_query("\xE3\x80\x80 稻香 。。\xE3\x80\x80") ==
         "稻香");
  assert(extract_song_search_query("你的答案") == "你的答案");
  assert(extract_song_search_query("播放你的答案") == "你的答案");
  assert(extract_song_search_query("一首歌") == "一首歌");
  assert(extract_song_search_query("播放。 ").empty());
  assert(extract_song_search_query(" ！？ ").empty());

  std::string message;
  WsTextAssembler assembler;
  assert(assembler.append(1U, true, 6U, "ab", 2U, &message) ==
         WsTextAssemblyResult::NeedMore);
  assert(assembler.append(1U, true, 6U, "cd", 2U, &message) ==
         WsTextAssemblyResult::NeedMore);
  assert(assembler.append(1U, true, 6U, "ef", 2U, &message) ==
         WsTextAssemblyResult::MessageReady);
  assert(message == "abcdef");

  message.clear();
  assert(assembler.append(1U, false, 3U, "abc", 3U, &message) ==
         WsTextAssemblyResult::NeedMore);
  assert(assembler.append(0U, true, 3U, "def", 3U, &message) ==
         WsTextAssemblyResult::MessageReady);
  assert(message == "abcdef");

  message.clear();
  assert(assembler.append(0U, true, 1U, "x", 1U, &message) ==
         WsTextAssemblyResult::ProtocolError);
  assert(assembler.append(1U, true, WsTextAssembler::kMaxMessageBytes + 1U,
                          "x", 1U, &message) ==
         WsTextAssemblyResult::TooLarge);
  assert(assembler.append(1U, false, 1U, "a", 1U, &message) ==
         WsTextAssemblyResult::NeedMore);
  assert(assembler.append(1U, true, 1U, "b", 1U, &message) ==
         WsTextAssemblyResult::ProtocolError);
  return 0;
}
