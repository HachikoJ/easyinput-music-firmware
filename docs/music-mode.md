# Offline music mode

Music mode is a board-local, two-track Ogg Opus player. The ESP32-S3 reads the
encoded tracks from a dedicated flash partition, decodes them on the board and
writes PCM to the board speaker. Playback does not require Wi-Fi, a computer or
a background companion process.

## Controls

- Rapidly short-press the encoder twice, with no more than 250 ms between
  presses, to enter music mode. Repeat the gesture to exit.
- While music mode is active, `S1` through `S8` are reserved for music and do
  not run their normal shortcuts, PTT or HID actions.
- The first press of any `S1` through `S8` starts track 0. Each later press
  switches to the next track. The two-track playlist wraps back to track 0.
- A single encoder press pauses or resumes the current track.
- Playback starts at 15% volume. Rotate left to lower it and right to raise it,
  in 5% steps across the 0-100% range.
- D1 through D5 show a changing multi-color breathing effect driven by the
  volume-scaled PCM RMS and beat envelope. Pause holds a low-brightness color
  pattern; leaving music mode stops the music visualization.

The existing non-music controls are restored when music mode exits.

## Private build inputs

Source tracks and generated music images are deliberately not stored in this
repository. Commercial recordings are not licensed for redistribution by this
project and must not be committed, attached to a release or published with the
source tree.

Enable the player only for a private firmware build and pass two absolute paths
to approved 48 kHz mono Ogg Opus files:

```sh
idf.py -B build-music \
  -D EASY_INPUT_MUSIC_PLAYER=ON \
  -D EASY_INPUT_SPEAKER_ASSETS_PRODUCT=OFF \
  -D EASY_INPUT_MUSIC_TRACK_0=/absolute/path/to/track-0.ogg \
  -D EASY_INPUT_MUSIC_TRACK_1=/absolute/path/to/track-1.ogg \
  build
```

The build runs `tools/build_music_image.py`, creates
`build-music/music.bin`, and registers that image with the normal ESP-IDF flash
target. The image remains a local build artifact and must not be committed.

The input files must fit the fixed image format; the packer fails the build
instead of truncating a track or overflowing its partition. Track order is
defined by `EASY_INPUT_MUSIC_TRACK_0` then `EASY_INPUT_MUSIC_TRACK_1`.

## Flash and memory budget

The canonical 16 MiB flash layout preserves the existing NVS, PHY, 3 MiB
factory application, `sound_a` and `sound_b` offsets. It adds one read-only
`music` partition at `0x430000` with size `0x400000` (4 MiB). The partition ends
at `0x830000`, leaving about 7.81 MiB of flash outside all declared partitions.

Playback memory-maps encoded track bytes from the read-only partition rather
than copying complete songs into RAM. Decoding and I2S output use bounded frame
buffers, preserving internal SRAM for DMA, Wi-Fi/Bluetooth services and task
stacks. The 15% default is a conservative startup policy, not a claim that
every speaker or recording has identical loudness.

GPIO8 is the shared active-high LED, microphone and speaker power rail. Music
playback must acquire the existing audio/power ownership path before enabling
the speaker and must release it when stopped. D1-D5 are the five WS2812 pixels
on GPIO12; the breathing effect changes pixel data and does not toggle GPIO8.
