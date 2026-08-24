# Music partition image format

The optional offline player uses one immutable 4 MiB custom partition named
`music` at Flash offset `0x430000` (type `0x40`, subtype `0x02`). Commercial
audio is external build input and is not stored in this repository.

Enable the image only for a local production build:

```sh
idf.py -B build-music \
  -D EASY_INPUT_MUSIC_PLAYER=ON \
  -D EASY_INPUT_MUSIC_TRACK_0=/path/to/first.ogg \
  -D EASY_INPUT_MUSIC_TRACK_1=/path/to/second.ogg \
  build
```

Both inputs must be Ogg Opus. `tools/build_music_image.py` creates
`music.bin` in the build directory and the normal ESP-IDF `flash` target adds
it at `0x430000`. The output is always exactly `0x400000` bytes; unused bytes
are `0xFF`.

## Header

The first 4 KiB is the header. Integers are unsigned little-endian values.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| `0x0000` | 8 | Magic `EIMUSIC\0` |
| `0x0008` | 4 | Format version, currently `1` |
| `0x000C` | 4 | Header size, `0x1000` |
| `0x0010` | 4 | Track count, exactly `2` |
| `0x0014` | 4 | Reserved, zero |
| `0x0018` | 40 | Track 0: offset, length, SHA-256 |
| `0x0040` | 40 | Track 1: offset, length, SHA-256 |
| `0x0068` | 3992 | Reserved, `0xFF` |

Each track begins on a 4 KiB boundary. Firmware validates the header and all
ranges during `MusicLibrary::begin()`, then maps only the requested track with
`esp_partition_mmap()`. The SHA-256 values support host-side image and
pre-flash verification; firmware deliberately does not hash a complete song
in the key-press path. A returned view remains valid until the next `track()`
call, `close()`, or library destruction.
