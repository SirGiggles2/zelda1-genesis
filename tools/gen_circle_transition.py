"""Original procedural H32 iris masks, shared by closing/opening transitions.

The 512x512 ROM canvas is cropped at runtime around a tile-aligned Link.
Horizontal/vertical pattern flips keep each radius within 96 spare VRAM tiles.
No source-game art; standard library only. Run from any directory.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate():
    lines = ['/* Generated original geometry: tools/gen_circle_transition.py. */']
    counts = []
    for frame in range(44):
        radius = frame * 8
        patterns = [(0xffffffff,) * 8]
        indices = {patterns[0]: 0}
        cells = []
        for ty in range(64):
            for tx in range(64):
                pixels = [[0 if (2*(tx*8+x)-511)**2 + (2*(ty*8+y)-511)**2 <= (radius*2)**2
                           and radius else 15 for x in range(8)] for y in range(8)]
                variants = []
                for flip in range(4):
                    rows = pixels[::-1] if flip & 2 else pixels
                    words = []
                    for row in rows:
                        values = row[::-1] if flip & 1 else row
                        word = 0
                        for value in values:
                            word = (word << 4) | value
                        words.append(word)
                    variants.append(tuple(words))
                pattern = min(variants)
                if not any(pattern):
                    cells.append(0)
                    continue
                if pattern not in indices:
                    indices[pattern] = len(patterns)
                    patterns.append(pattern)
                flip = variants.index(pattern)
                cells.append(0xe000 | (0x800 if flip & 1 else 0) |
                             (0x1000 if flip & 2 else 0) | (1440 + indices[pattern]))
        assert len(patterns) <= 96, (frame, len(patterns))
        counts.append(len(patterns))
        lines.append('static const unsigned long circle_patterns_%d[] = {\n%s\n};' %
                     (frame, '\n'.join('    ' + ','.join('0x%08xUL' % v for v in p) + ',' for p in patterns)))
        lines.append('static const unsigned short circle_map_%d[4096] = {\n%s\n};' %
                     (frame, '\n'.join('    ' + ','.join('0x%04xu' % v for v in cells[i:i+64]) + ',' for i in range(0,4096,64))))
    lines.append('static const circle_frame_t circle_frames[44] = {')
    lines.extend('    {circle_patterns_%d,circle_map_%d,%du},' % (f,f,n) for f,n in enumerate(counts))
    lines.append('};\n')
    (ROOT / 'src/game/world/circle_transition_masks.inc').write_text('\n'.join(lines), encoding='ascii')
    print('44 radii; peak %d patterns, %d mask DMA bytes' % (max(counts),max(counts)*32+1792))


if __name__ == '__main__':
    generate()
