"""The PSP CLUT preserves source indexed colours, including their alpha."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_skinned_model import indexed_texture


class IndexedTextureTests(unittest.TestCase):
    def roundtrip(self, colors, width, height):
        source = bytes(channel for color in colors for channel in color)
        pixels, palette = indexed_texture({"width": width, "height": height, "rgba": source})
        self.assertEqual(len(pixels), width * height)
        self.assertEqual(len(palette), 256)
        self.assertTrue(all(0 <= index < 256 for index in pixels))
        return pixels, palette, b"".join(struct.pack("<I", palette[index]) for index in pixels), source

    def test_full_palette_is_byte_exact_and_deterministic(self):
        colors = [(i, (i * 73) % 256, 255 - i, (i * 19) % 256) for i in range(256)]
        colors += colors[::-1]
        pixels, palette, decoded, source = self.roundtrip(colors, 32, 16)
        self.assertEqual(decoded, source)
        self.assertEqual(pixels[:256], list(range(256)))
        self.assertEqual((pixels, palette), indexed_texture({"width": 32, "height": 16, "rgba": source}))

    def test_equal_rgb_different_alpha_keeps_distinct_entries(self):
        colors = [(80, 90, 100, alpha) for alpha in (0, 1, 31, 32, 33, 127, 128, 255)]
        pixels, palette, decoded, source = self.roundtrip(colors, 4, 2)
        self.assertEqual(decoded, source)
        self.assertEqual(len(set(pixels)), 8)
        self.assertEqual(palette[8:], [0] * 248)

    def test_truecolor_fallback_keeps_dimensions_and_valid_indices(self):
        colors = [(i % 256, i // 256, (i * 17) % 256, 255) for i in range(512)]
        pixels, palette, decoded, source = self.roundtrip(colors, 32, 16)
        self.assertEqual(len(decoded), len(source))
        self.assertLessEqual(len(set(pixels)), 256)
        self.assertNotEqual(decoded, source)  # 512 distinct colours cannot fit a 256-entry CLUT.


if __name__ == "__main__":
    unittest.main()
