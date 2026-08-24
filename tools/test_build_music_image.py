#!/usr/bin/env python3
"""Self-tests for the deterministic EasyInput music image builder."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import build_music_image


def fake_ogg_opus(payload: bytes) -> bytes:
    return b"OggS" + bytes(24) + b"OpusHead" + bytes(20) + payload


class MusicImageTests(unittest.TestCase):
    def test_builds_aligned_hashed_fixed_size_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tracks = [fake_ogg_opus(b"first"), fake_ogg_opus(b"second" * 800)]
            paths = [root / "first.ogg", root / "second.ogg"]
            for path, data in zip(paths, tracks):
                path.write_bytes(data)
            output = root / "music.bin"

            build_music_image.build_image(paths, output)
            image = output.read_bytes()

            self.assertEqual(len(image), build_music_image.PARTITION_SIZE)
            magic, version, header_size, count, reserved = (
                build_music_image.FIXED_HEADER.unpack_from(image)
            )
            self.assertEqual(magic, build_music_image.MAGIC)
            self.assertEqual(version, build_music_image.VERSION)
            self.assertEqual(header_size, build_music_image.HEADER_SIZE)
            self.assertEqual(count, 2)
            self.assertEqual(reserved, 0)

            cursor = build_music_image.FIXED_HEADER.size
            for expected in tracks:
                offset, length, digest = struct.unpack_from("<II32s", image, cursor)
                self.assertEqual(offset % build_music_image.HEADER_SIZE, 0)
                self.assertEqual(length, len(expected))
                self.assertEqual(digest, hashlib.sha256(expected).digest())
                self.assertEqual(image[offset : offset + length], expected)
                cursor += build_music_image.ENTRY_SIZE

    def test_rejects_non_opus_input(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bad = root / "bad.ogg"
            good = root / "good.ogg"
            bad.write_bytes(b"not ogg opus")
            good.write_bytes(fake_ogg_opus(b"ok"))
            with self.assertRaisesRegex(ValueError, "expected an Ogg stream"):
                build_music_image.build_image(
                    [bad, good], root / "music.bin"
                )

    def test_rejects_partition_overflow(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.ogg"
            second = root / "second.ogg"
            first.write_bytes(
                fake_ogg_opus(bytes(build_music_image.PARTITION_SIZE - 0x2000))
            )
            second.write_bytes(fake_ogg_opus(b"too much"))
            with self.assertRaisesRegex(ValueError, "tracks exceed"):
                build_music_image.build_image(
                    [first, second], root / "music.bin"
                )


if __name__ == "__main__":
    unittest.main()
