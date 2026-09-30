#!/usr/bin/env python3
"""make-synthetic.py - a game of the core's own, in Another World's DOS format.

The gate needs content, and Another World's data is not the core's to carry.
So the gate also runs this: a four-part "game" written for rawgl's engine from
scratch - its own bytecode, palettes, polygons, a sound and a music module - in
the DOS release's layout (MEMLIST.BIN and banks), which exercises the path a
real game takes through the core:

  16000 (where the engine starts a release with a password screen) - a square
        crossing the screen every 40 ms; the fire button goes on.
  16001 - 25 frames of 80 ms: the music starts (its patterns tell the script
        their row through VAR_MUSIC_SYNC, and a marker shows it), then the
        game goes to 16003 by itself.
  16003 - the joystick moves a square, fire plays a sound, a marker sits where
        the random seed (the randomSeed setting) puts it, another follows the
        music; P pauses (the engine's pause), C goes to the password screen.
  16008 - the password screen: a letter typed puts a square at its place in
        the alphabet; fire goes back to 16003.

The files go into a zip the way a person would make one - in a folder, the
names in capitals - so the core's folder finding and its case-insensitive
names are exercised too.

Two broken variants test what a fault in the game's data does to the machine:
--bad-opcode ends the intro on an opcode rawgl does not have (its error()),
--bad-shape draws a polygon of more vertices than rawgl allows (its assertion).
Either halts the machine; the gate checks it keeps stepping, halted.

usage: make-synthetic.py [--bad-opcode | --bad-shape] <out.zip>
"""

import struct
import sys
import zipfile

RT_SOUND, RT_MUSIC, RT_BITMAP, RT_PALETTE, RT_BYTECODE, RT_SHAPE = 0, 1, 2, 3, 4, 5

VAR_RANDOM_SEED = 0x3C
VAR_LAST_KEYCHAR = 0xDA
VAR_MUSIC_SYNC = 0xF4
VAR_HERO_ACTION = 0xFA
VAR_HERO_POS_JUMP_DOWN = 0xFB
VAR_HERO_POS_LEFT_RIGHT = 0xFC
VAR_PAUSE_SLICES = 0xFF


class Asm:
    """rawgl's opcodes (staticres.cpp Script::_opTable), with labels."""

    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []

    def label(self, name):
        self.labels[name] = len(self.code)

    def _b(self, *bs):
        self.code += bytes(b & 0xFF for b in bs)

    def _w(self, w):
        self.code += struct.pack(">H", w & 0xFFFF)

    def _ref(self, name):
        self.fixups.append((len(self.code), name))
        self._w(0)

    def mov_const(self, var, n): self._b(0x00, var); self._w(n)
    def mov(self, a, b): self._b(0x01, a, b)
    def add(self, a, b): self._b(0x02, a, b)
    def add_const(self, var, n): self._b(0x03, var); self._w(n)
    def yield_task(self): self._b(0x06)
    def jmp(self, name): self._b(0x07); self._ref(name)
    def jmp_if_var(self, var, name): self._b(0x09, var); self._ref(name)

    def jmp_if_eq_const(self, var, n, name):
        # condJmp: op 0 (==), a byte constant
        self._b(0x0A, 0x00, var, n); self._ref(name)

    def set_palette(self, num): self._b(0x0B, num, 0)   # the word's high byte is the palette
    def select_page(self, p): self._b(0x0D, p)
    def fill_page(self, p, color): self._b(0x0E, p, color)
    def update_display(self, p): self._b(0x10, p)
    def and_(self, var, n): self._b(0x14, var); self._w(n)
    def shl(self, var, n): self._b(0x16, var); self._w(n)
    def play_sound(self, res, freq, vol, channel): self._b(0x18); self._w(res); self._b(freq, vol, channel)
    def update_resources(self, num): self._b(0x19); self._w(num)
    def play_music(self, res, delay, pos): self._b(0x1A); self._w(res); self._w(delay); self._b(pos)

    def draw_at_vars(self, shape_offset, xvar, yvar):
        # 0x40 | 0x10 (x from a variable) | 0x04 (y from a variable), zoom 64
        assert shape_offset % 2 == 0
        self._b(0x54); self._w(shape_offset // 2); self._b(xvar, yvar)

    def build(self):
        for at, name in self.fixups:
            struct.pack_into(">H", self.code, at, self.labels[name])
        return bytes(self.code)


SHAPE_BIG, SHAPE_SMALL = 0, 16


SHAPE_BAD = 32


def shapes():
    def rect(color, w, h):
        return bytes([0xC0 | color, w, h, 4, w, 0, w, h, 0, h, 0, 0])
    data = bytearray(rect(4, 20, 20))
    data += bytes(SHAPE_SMALL - len(data))
    data += rect(5, 8, 8)
    data += bytes(SHAPE_BAD - len(data))
    # 70 vertices: one more than rawgl's QuadStrip holds (MAX_VERTICES)
    data += bytes([0xC0 | 6, 40, 40, 70]) + bytes(140)
    return bytes(data)


def palettes():
    # 32 palettes of 16 0x0RGB words (the VGA half), then the EGA half
    base = [0x000, 0xF00, 0x0F0, 0x00F, 0xFFF, 0xFF0, 0x0FF, 0xF0F,
            0x888, 0x800, 0x080, 0x008, 0x444, 0x880, 0x088, 0x808]
    out = bytearray()
    for p in range(32):
        cols = list(base)
        cols[0] = [0x000, 0x224, 0x420, 0x042][p % 4]   # each part's background
        for c in cols:
            out += struct.pack(">H", c)
    return bytes(out) + bytes(1024)


def sound():
    # a raw sound: length / 2, loop length / 2, 4 unused bytes, signed 8-bit
    n = 2000
    samples = bytes((0x40 if (i // 25) % 2 else 0xC0) for i in range(n))
    return struct.pack(">HH", n // 2, 0) + bytes(4) + samples


def music():
    # delay (a tick of about 100 ms), 15 instruments (resource, volume), the
    # order count, the order table, then 64-row patterns of 4 channels
    mod = bytearray(0xC0)
    struct.pack_into(">H", mod, 0, 12000)
    struct.pack_into(">HH", mod, 2, 0x01, 40)      # instrument 1: sound 0x01
    mod[0x3F] = 2
    mod[0x40] = 0
    mod[0x41] = 0
    pattern = bytearray(1024)
    for row in range(64):
        base = row * 16
        if row % 4 == 0:
            period = [428, 381, 339, 320][(row // 4) % 4]
            struct.pack_into(">HH", pattern, base + 0, period, 0x1000)
        struct.pack_into(">HH", pattern, base + 4, 0xFFFD, row)   # the row, to the script
    return bytes(mod + pattern)


def part_protection():
    a = Asm()
    a.set_palette(3)
    a.mov_const(VAR_PAUSE_SLICES, 2)
    a.mov_const(1, 0)
    a.mov_const(2, 100)
    a.label("loop")
    a.select_page(0)
    a.fill_page(0, 0)
    a.add_const(1, 3)
    a.draw_at_vars(SHAPE_BIG, 1, 2)
    a.jmp_if_eq_const(VAR_HERO_ACTION, 0, "show")
    a.update_resources(16001)
    a.label("show")
    a.update_display(0)
    a.yield_task()
    a.jmp("loop")
    return a.build()


def music_marker(a):
    a.mov(5, VAR_MUSIC_SYNC)
    a.shl(5, 2)
    a.add_const(5, 20)
    a.mov_const(6, 180)
    a.draw_at_vars(SHAPE_SMALL, 5, 6)


BROKEN = None


def part_intro():
    a = Asm()
    a.set_palette(1)
    a.update_resources(0x01)
    a.update_resources(0x02)
    a.play_music(0x02, 0, 0)
    a.mov_const(VAR_PAUSE_SLICES, 4)
    a.mov_const(0x10, 25)
    a.mov_const(1, 40)
    a.mov_const(2, 100)
    a.label("loop")
    a.select_page(0)
    a.fill_page(0, 0)
    a.draw_at_vars(SHAPE_BIG, 1, 2)
    a.add_const(1, 8)
    music_marker(a)
    if BROKEN == "shape":
        a.jmp_if_eq_const(0x10, 10, "bad")
        a.jmp("show")
        a.label("bad")
        a.draw_at_vars(SHAPE_BAD, 1, 2)
        a.label("show")
    a.update_display(0)
    a.yield_task()
    a.jmp_if_var(0x10, "loop")
    if BROKEN == "opcode":
        a._b(0x1B)   # past the last opcode (0x1A, playMusic)
    a.update_resources(16003)
    a.label("wait")
    a.yield_task()
    a.jmp("wait")
    return a.build()


def part_play():
    a = Asm()
    a.set_palette(2)
    a.update_resources(0x01)
    a.update_resources(0x02)
    a.play_music(0x02, 0, 0)
    a.mov_const(VAR_PAUSE_SLICES, 4)
    a.mov_const(1, 160)
    a.mov_const(2, 100)
    a.mov(4, VAR_RANDOM_SEED)
    a.and_(4, 0xFF)
    a.mov_const(7, 30)
    a.label("loop")
    a.select_page(0)
    a.fill_page(0, 0)
    a.draw_at_vars(SHAPE_BIG, 1, 2)
    a.draw_at_vars(SHAPE_SMALL, 4, 7)
    music_marker(a)
    a.mov(8, VAR_HERO_POS_LEFT_RIGHT)
    a.shl(8, 2)
    a.add(1, 8)
    a.mov(8, VAR_HERO_POS_JUMP_DOWN)
    a.shl(8, 2)
    a.add(2, 8)
    a.jmp_if_eq_const(VAR_HERO_ACTION, 0, "show")
    a.play_sound(0x01, 20, 63, 0)
    a.label("show")
    a.update_display(0)
    a.yield_task()
    a.jmp("loop")
    return a.build()


def part_password():
    a = Asm()
    a.set_palette(3)
    a.mov_const(VAR_PAUSE_SLICES, 4)
    a.mov_const(9, 64)
    a.mov_const(2, 100)
    a.label("loop")
    a.jmp_if_eq_const(VAR_LAST_KEYCHAR, 0, "nokey")
    a.mov(9, VAR_LAST_KEYCHAR)
    a.label("nokey")
    a.mov(1, 9)
    a.add_const(1, -64)
    a.shl(1, 3)
    a.add_const(1, 40)
    a.select_page(0)
    a.fill_page(0, 0)
    a.draw_at_vars(SHAPE_BIG, 1, 2)
    a.jmp_if_eq_const(VAR_HERO_ACTION, 0, "show")
    a.update_resources(16003)
    a.label("show")
    a.update_display(0)
    a.yield_task()
    a.jmp("loop")
    return a.build()


def build():
    pal, shp = palettes(), shapes()
    res = {  # number: (type, data, bank)
        0x01: (RT_SOUND, sound(), 1),
        0x02: (RT_MUSIC, music(), 1),
        0x11: (RT_SHAPE, shp, 1),
        0x14: (RT_PALETTE, pal, 1), 0x15: (RT_BYTECODE, part_protection(), 1), 0x16: (RT_SHAPE, shp, 1),
        0x17: (RT_PALETTE, pal, 1), 0x18: (RT_BYTECODE, part_intro(), 1), 0x19: (RT_SHAPE, shp, 1),
        0x1D: (RT_PALETTE, pal, 1), 0x1E: (RT_BYTECODE, part_play(), 1), 0x1F: (RT_SHAPE, shp, 1),
        # the password screen's code in a bank of its own: its being there is
        # what tells the engine the release has a password screen (and so a
        # copy protection to start at)
        0x7D: (RT_PALETTE, pal, 1), 0x7E: (RT_BYTECODE, part_password(), 0x0D), 0x7F: (RT_SHAPE, shp, 1),
    }
    banks = {}
    memlist = bytearray()
    for num in range(0x80):
        if num in res:
            rtype, data, bank = res[num]
            b = banks.setdefault(bank, bytearray())
            pos = len(b)
            b += data
            memlist += struct.pack(">BBIBBIII", 0, rtype, 0, 0, bank, pos, len(data), len(data))
        else:
            memlist += struct.pack(">BBIBBIII", 0, RT_SOUND, 0, 0, 0, 0, 0, 0)
    memlist += struct.pack(">BBIBBIII", 0xFF, 0, 0, 0, 0, 0, 0, 0)
    files = {"MEMLIST.BIN": bytes(memlist)}
    for bank, data in banks.items():
        files["BANK%02X" % bank] = bytes(data)
    return files


def main():
    global BROKEN
    args = sys.argv[1:]
    if args and args[0] in ("--bad-opcode", "--bad-shape"):
        BROKEN = args.pop(0)[6:]
    if len(args) != 1:
        sys.exit(__doc__)
    files = build()
    with zipfile.ZipFile(args[0], "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo("SYNTH/", date_time=(1991, 1, 1, 0, 0, 0)), b"")
        for name in sorted(files):
            info = zipfile.ZipInfo("SYNTH/" + name, date_time=(1991, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, files[name])
        z.writestr(zipfile.ZipInfo("README.TXT", date_time=(1991, 1, 1, 0, 0, 0)), b"the core's synthetic test game\n")


if __name__ == "__main__":
    main()
