#!/usr/bin/env python3
"""Build the fixed two-track EasyInput music partition image."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile


MAGIC = b"EIMUSIC\0"
VERSION = 1
HEADER_SIZE = 0x1000
PARTITION_SIZE = 0x400000
TRACK_COUNT = 2
ENTRY_SIZE = 40
FIXED_HEADER = struct.Struct("<8sIIII")
TRACK_ENTRY = struct.Struct("<II32s")


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def validate_ogg_opus(path: Path, data: bytes) -> None:
    if len(data) < 36 or not data.startswith(b"OggS"):
        raise ValueError(f"{path.name}: expected an Ogg stream")
    if b"OpusHead" not in data[:65536]:
        raise ValueError(f"{path.name}: expected an Ogg Opus stream")


def build_image(track_paths: list[Path], output: Path) -> None:
    if len(track_paths) != TRACK_COUNT:
        raise ValueError(f"exactly {TRACK_COUNT} tracks are required")

    tracks: list[bytes] = []
    entries: list[tuple[int, int, bytes]] = []
    next_offset = HEADER_SIZE
    for path in track_paths:
        data = path.read_bytes()
        validate_ogg_opus(path, data)
        offset = align_up(next_offset, HEADER_SIZE)
        end = offset + len(data)
        if end > PARTITION_SIZE:
            raise ValueError(
                f"tracks exceed the {PARTITION_SIZE}-byte music partition"
            )
        tracks.append(data)
        entries.append((offset, len(data), hashlib.sha256(data).digest()))
        next_offset = end

    image = bytearray(b"\xff" * PARTITION_SIZE)
    image[: FIXED_HEADER.size] = FIXED_HEADER.pack(
        MAGIC, VERSION, HEADER_SIZE, TRACK_COUNT, 0
    )
    entry_offset = FIXED_HEADER.size
    for entry in entries:
        image[entry_offset : entry_offset + ENTRY_SIZE] = TRACK_ENTRY.pack(*entry)
        entry_offset += ENTRY_SIZE
    for data, (offset, length, _) in zip(tracks, entries):
        image[offset : offset + length] = data

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="wb", dir=output.parent, prefix=f".{output.name}.", delete=False
    ) as temporary:
        temporary.write(image)
        temporary_path = Path(temporary.name)
    os.replace(temporary_path, output)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a 4 MiB EasyInput two-track music image"
    )
    parser.add_argument("--track0", required=True, type=Path)
    parser.add_argument("--track1", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        build_image([args.track0, args.track1], args.output)
    except (OSError, ValueError) as error:
        raise SystemExit(f"music image generation failed: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
