#pragma once

#include <cstdint>
#include <string>

namespace easy_input::online_music {

// Resolves a recognized song title to a direct MP3 stream URL. Results are
// intentionally transient: neither the title nor provider URL is persisted.
class MusicCatalog {
 public:
  bool search(const std::string& title, std::string* song_id) const;
  bool search(const std::string& title,
              std::uint32_t page,
              std::string* song_id) const;
  bool resolve_url(const std::string& song_id,
                   std::string* stream_url) const;
  bool resolve(const std::string& title, std::string* stream_url) const;
};

}  // namespace easy_input::online_music
