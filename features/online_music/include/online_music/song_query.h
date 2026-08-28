#pragma once

#include <string>
#include <string_view>

namespace easy_input::online_music {

std::string extract_song_search_query(std::string_view transcript);

}  // namespace easy_input::online_music
