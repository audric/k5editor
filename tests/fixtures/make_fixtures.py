#!/usr/bin/env python3
"""Builds the memory images used by the tests.

Everything in them is made up; no real radio was dumped. The bytes are written
here by hand, independently of the C++ code under test, following the layout in
the F4HWN CHIRP driver.
"""
import struct
from pathlib import Path

HERE = Path(__file__).parent
ATTR, NAMES = 0x0D60, 0x0F50


def blank():
    return bytearray(b"\xff" * 0x2000)


def channel(img, slot, name, mhz, *, offset=0.0, shift=0, rx=(0, 0), tx=(0, 0), mod=0,
            narrow=False, power=0, step=4, lists=0, band):
    """slot is 1-based; rx/tx are (type, code); band is the marker stored in the attribute byte."""
    i = slot - 1
    rec = struct.pack("<II", round(mhz * 1e5), round(offset * 1e5))
    rec += bytes([rx[1], tx[1], tx[0] << 4 | rx[0], mod << 4 | shift, power << 2 | (2 if narrow else 0), 0, step, 0])
    img[i * 16:i * 16 + 16] = rec
    img[ATTR + i] = lists << 5 | band
    img[NAMES + i * 16:NAMES + i * 16 + 16] = name.ljust(10).encode("ascii") + bytes(6)


def put(img, offset, hexbytes):
    data = bytes.fromhex(hexbytes)
    img[offset:offset + len(data)] = data


def healthy():
    img = blank()
    channel(img, 1, "CH ONE", 446.00625, lists=1, band=5)
    channel(img, 2, "RPT VHF", 145.600, offset=0.6, shift=2, tx=(1, 8), power=7, band=2)
    channel(img, 3, "RPT UHF", 439.000, offset=1.6, shift=1, tx=(1, 12), power=7, band=5)
    channel(img, 4, "AIR", 118.100, mod=1, narrow=True, power=6, step=5, band=1)
    channel(img, 5, "TONE", 433.500, tx=(1, 16), power=6, step=1, lists=1, band=5)
    channel(img, 6, "DCS", 433.525, rx=(2, 0), tx=(3, 103), lists=2, band=5)
    channel(img, 7, "SIDEBAND", 28.500, mod=2, narrow=True, step=11, band=0)
    channel(img, 8, "OLD BAND", 27.125, mod=1, step=3, lists=2, band=1)     # marker should be 0
    channel(img, 9, "MARINE", 156.800, step=5, band=2)
    channel(img, 10, "WIDE RX", 868.000, step=5, lists=7, band=6)
    # slots 11..200 stay empty

    put(img, 0x0E70, "00010b0000000003" "0a02000400050001")     # squelch 1, 1 min timeout, mic +12 dB, backlight 0..10 ...
    put(img, 0x0E90, "0002030106020001" "ffffffff")              # side keys, scan resume, no password
    put(img, 0x0EA0, "ff824c")                                    # voice unset, S0 -130, S9 -76
    put(img, 0x0EA8, "0000000100")                                # main VFO B, battery type 0
    img[0x0EB0:0x0EC0] = b"K5 EDITOR".ljust(12, b"\0") + b"\x00\xff\xff\xff"
    img[0x0EC0:0x0ED0] = b"TEST".ljust(12, b"\0") + b"\x00\xff\xff\xff"
    put(img, 0x0ED0, "01000001001e0a0a0a0aff")                    # DTMF timings
    img[0x0EE0:0x0F18] = (b"102" + bytes(5) + b"77777" + bytes(3) + b"88888" + bytes(3) +
                          b"123" + bytes(13) + b"456" + bytes(13))
    put(img, 0x0F18, "0000ffff00ffffff")                          # scan lists: no priority channels
    put(img, 0x0F40, "0affffffff000033")                          # F Lock = UNLOCK ALL, mic bar on
    put(img, 0x1FF0, "2033ffff79ff0000")                          # build options, extras (0x1FF5 never written)
    return img


def damaged():
    """Channels as another firmware or a failed write might leave them."""
    img = blank()
    channel(img, 4, "BAD ONE", 0.02570, band=5)
    channel(img, 5, "GOOD ONE", 446.05625, band=5)
    channel(img, 25, "BAD TWO", 0.00066, band=2)
    return img


if __name__ == "__main__":
    (HERE / "f4hwn_v43.bin").write_bytes(healthy())
    (HERE / "damaged.bin").write_bytes(damaged())
    print("fixtures written")
