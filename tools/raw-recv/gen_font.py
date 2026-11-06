#!/usr/bin/env python3
"""Render the viewer's HarmonyOS Sans atlas without changing the source TTFs.

Requires Pillow. Pass --font-dir when the original downloaded font package
is elsewhere. The generated header is checked in, so normal C builds need
neither Python nor installed fonts. Font attribution is embedded in the header.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# Repository root used to locate source text and the downloaded font package.
ROOT = Path(__file__).resolve().parents[2]
# Desktop source directory whose literal text determines the needed glyphs.
HERE = Path(__file__).resolve().parent
# Width cap compatible with renderers that support 2048-pixel textures.
ATLAS_WIDTH = 2048


def main():
    """Read --font-dir/--preview, render both weights, and write font_harmony.h.

    Does not compile the viewer. Missing source fonts raise a file error.
    --preview optionally saves a PNG of the generated atlas for inspection.
    """
    # Generator arguments; the original font directory remains untouched.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font-dir', type=Path, default=ROOT / 'HarmonyOS-Sans/HarmonyOS Sans/HarmonyOS_Sans_SC')
    parser.add_argument('--preview', type=Path)
    # Resolved font source and optional preview destination.
    args = parser.parse_args()
    # Source characters, including ASCII for paths, addresses, numbers and keys.
    chars = set(chr(code) for code in range(32, 127))
    for source in sorted([*HERE.glob('*.h'), *HERE.glob('*.c')]):  # Each desktop source file, excluding generated atlases.
        if source.name.startswith('font_'):
            continue
        chars.update(character for character in source.read_text() if ord(character) >= 128)
    # Stable Unicode order allows binary search in the viewer.
    codes = sorted(ord(character) for character in chars)
    # Per-weight glyph image and metrics (x, y, width, height, advance).
    styles = []
    # Next unused atlas position and height of the current shelf, in pixels.
    x, y, shelf = 0, 0, 0
    # Original font hashes recorded for reproducible regeneration.
    hashes = []
    for weight, size in [('Regular', 32), ('Bold', 56)]:  # Body and title font weights at their maximum drawing sizes.
        # Unmodified original font file and its rasterizer.
        path = args.font_dir / f'HarmonyOS_Sans_SC_{weight}.ttf'
        font = ImageFont.truetype(str(path), size)
        hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
        # Full line box preserves a common baseline for Latin and Chinese.
        ascent, descent = font.getmetrics()
        # Glyph data for this weight, sharing one packed texture.
        glyphs = []
        for code in codes:  # Unicode scalar whose bitmap is being rendered.
            # Character and horizontal advance used for measurement and drawing.
            char = chr(code)
            advance = round(font.getlength(char))
            # Ink bounds, including any left overhang in the raster origin.
            left, top, right, bottom = font.getbbox(char)
            # Width and height of the baseline-aligned glyph bitmap.
            width, height = max(right, advance, 1) - min(left, 0), ascent + descent
            if x + width + 1 > ATLAS_WIDTH:
                x, y, shelf = 0, y + shelf + 1, 0
            # White coverage mask rendered from the original TTF.
            bitmap = Image.new('L', (width, height))
            ImageDraw.Draw(bitmap).text((-min(left, 0), 0), char, font=font, fill=255)
            glyphs.append((bitmap, (x, y, width, height, advance)))
            x += width + 1
            shelf = max(shelf, height)
        styles.append(glyphs)
    # Final atlas height, rounded up only by the current glyph shelf.
    height = y + shelf
    if height > 2048:
        raise ValueError(f'Atlas height {height} exceeds the 2048-pixel limit')
    # Single texture shared by both weights.
    atlas = Image.new('L', (ATLAS_WIDTH, height))
    for glyphs in styles:  # One font weight's glyph placements.
        for bitmap, metrics in glyphs:  # Coverage mask and destination rectangle.
            atlas.paste(bitmap, metrics[:2])
    # Four-bit coverage packed in pairs, then run-length encoded as count/value.
    pixels = atlas.tobytes()
    packed = bytes((pixels[index] & 0xF0) | (pixels[index + 1] >> 4) for index in range(0, len(pixels), 2))
    # Encoded count/value pairs, each count in 1..255.
    encoded = bytearray()
    # Current byte run start within the packed coverage stream.
    index = 0
    while index < len(packed):
        # First byte after this run, capped to fit one count byte.
        end = index + 1
        while end < min(index + 255, len(packed)) and packed[end] == packed[index]:
            end += 1
        encoded.extend((end - index, packed[index]))
        index = end
    # Generated source header; the licence travels inside the binary as well.
    licenseText = (args.font_dir / 'LICENSE.txt').read_text().split('\0', 1)[0].strip()
    # C string escaping keeps source font notices exact and readable.
    with (HERE / 'font_harmony.h').open('w') as output:  # Destination header, rewritten by this regeneration run.
        output.write('/* Generated by gen_font.py; HarmonyOS Sans SC, Regular 32px / Bold 56px.\n')
        output.write(' * Original TTF SHA-256: ' + ' / '.join(hashes) + '\n */\n')
        output.write('#define FONT_ATLAS_W %d\n#define FONT_ATLAS_H %d\n#define FONT_COUNT %d\n' % (ATLAS_WIDTH, height, len(codes)))
        output.write('/* Original copyright and licence, displayed in the viewer font notice. */\nstatic const char g_fontLicense[] = ' + json.dumps(licenseText, ensure_ascii=True) + ';\n')
        output.write('/* Rendered em sizes for regular and bold text. */\nstatic const int font_px[2] = {32,56};\n')
        output.write('/* Sorted Unicode scalars shared by both glyph tables. */\nstatic const unsigned int font_codes[FONT_COUNT] = {' + ','.join(map(str, codes)) + '};\n')
        output.write('/* Per weight and scalar: x, y, width, height, horizontal advance. */\nstatic const unsigned short font_glyphs[2][FONT_COUNT][5] = {\n')
        for glyphs in styles:  # Metrics for one weight in Unicode order.
            output.write('{' + ','.join('{%d,%d,%d,%d,%d}' % metrics for _, metrics in glyphs) + '},\n')
        output.write('};\n/* Run-length encoded, packed 4-bit coverage (count,value). */\nstatic const unsigned char font_atlas[] = {\n')
        for index in range(0, len(encoded), 40):  # Byte offset of the next generated source line.
            output.write(','.join(map(str, encoded[index:index + 40])) + ',\n')
        output.write('};\n')
    if args.preview:
        atlas.save(args.preview)
    print(f'{len(codes)} glyphs per weight; {ATLAS_WIDTH}x{height}; {len(encoded)} encoded bytes')


if __name__ == '__main__':
    main()
