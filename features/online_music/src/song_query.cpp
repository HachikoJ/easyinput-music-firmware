#include "online_music/song_query.h"

#include <array>

namespace easy_input::online_music {
namespace {

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0U, prefix.size()) == prefix;
}

void trim(std::string_view* value) {
  while (!value->empty() &&
         (value->front() == ' ' || value->front() == '\t' ||
          value->front() == '\r' || value->front() == '\n')) {
    value->remove_prefix(1U);
  }
  while (!value->empty() &&
         (value->back() == ' ' || value->back() == '\t' ||
          value->back() == '\r' || value->back() == '\n')) {
    value->remove_suffix(1U);
  }
  constexpr std::string_view kIdeographicSpace = "\xE3\x80\x80";
  while (starts_with(*value, kIdeographicSpace)) {
    value->remove_prefix(kIdeographicSpace.size());
  }
  while (value->size() >= kIdeographicSpace.size() &&
         value->substr(value->size() - kIdeographicSpace.size()) ==
             kIdeographicSpace) {
    value->remove_suffix(kIdeographicSpace.size());
  }
}

void trim_trailing_punctuation(std::string_view* value) {
  constexpr std::array<std::string_view, 13> kMarks = {
      "。", "！", "？", "，", "；", "：", "!", "?", ",", ";", ":", ".", "、"};
  bool removed = true;
  while (removed) {
    removed = false;
    trim(value);
    for (const auto mark : kMarks) {
      if (value->size() >= mark.size() &&
          value->substr(value->size() - mark.size()) == mark) {
        value->remove_suffix(mark.size());
        removed = true;
        break;
      }
    }
  }
}

}  // namespace

std::string extract_song_search_query(std::string_view transcript) {
  trim(&transcript);
  trim_trailing_punctuation(&transcript);
  constexpr std::array<std::string_view, 12> kPrefixes = {
      "请帮我播放", "帮我播放", "帮我放一首", "给我来一首",
      "请播放", "播放", "我想听", "想听", "来一首", "放一首",
      "来首", "放首"};
  bool command_removed = false;
  for (const auto prefix : kPrefixes) {
    if (starts_with(transcript, prefix)) {
      transcript.remove_prefix(prefix.size());
      trim(&transcript);
      command_removed = true;
      break;
    }
  }
  constexpr std::array<std::string_view, 2> kFillers = {"一下", "一首"};
  if (command_removed) {
    for (const auto filler : kFillers) {
      if (starts_with(transcript, filler)) {
        transcript.remove_prefix(filler.size());
        trim(&transcript);
        break;
      }
    }
  }
  trim_trailing_punctuation(&transcript);
  return std::string(transcript);
}

}  // namespace easy_input::online_music
