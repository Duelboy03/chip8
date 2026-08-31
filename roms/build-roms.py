#!/usr/bin/env python3
"""Assembles the two built-in demo ROMs. Run from ~/CHIP8: python3 roms/build-roms.py"""
import struct, pathlib

def asm(words):
    return b''.join(struct.pack('>H', w) for w in words)

# splash.ch8 -- draws the built-in font sprites for 0..F in a 4x4 grid.
splash = [
    0x00E0,  # 200 clear screen
    0x6200,  # 202 V2 = 0   (digit counter)
    0x6008,  # 204 V0 = 8   (x)
    0x6104,  # 206 V1 = 4   (y)
    # loop:
    0xF229,  # 208 I = font(V2)
    0xD015,  # 20A draw 5 rows at (V0,V1)
    0x700C,  # 20C V0 += 12
    0x7201,  # 20E V2 += 1
    0x8420,  # 210 V4 = V2
    0x6503,  # 212 V5 = 3
    0x8452,  # 214 V4 &= V5   (V2 mod 4)
    0x4400,  # 216 skip next if V4 != 0
    0x1220,  # 218 jump newline
    # check:
    0x3210,  # 21A skip next if V2 == 16
    0x1208,  # 21C jump loop
    0x121E,  # 21E done: spin
    # newline:
    0x6008,  # 220 V0 = 8
    0x7107,  # 222 V1 += 7
    0x121A,  # 224 jump check
]

# keytest.ch8 -- waits for a key and shows the hex digit it produced.
keytest = [
    0x6018,  # 200 V0 = 24 (x)
    0x610E,  # 202 V1 = 14 (y)
    # loop:
    0xF30A,  # 204 wait for key -> V3
    0x00E0,  # 206 clear screen
    0xF329,  # 208 I = font(V3)
    0xD015,  # 20A draw
    0x1204,  # 20C jump loop
]

here = pathlib.Path(__file__).parent
(here / 'splash.ch8').write_bytes(asm(splash))
(here / 'keytest.ch8').write_bytes(asm(keytest))
print('wrote splash.ch8, keytest.ch8')
