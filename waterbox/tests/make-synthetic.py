#!/usr/bin/env python3
"""make-synthetic.py - a game of the core's own, in each release's format.

The gate needs content, and Another World's data is not the core's to carry.
So the gate also runs this: a four-part "game" written for rawgl's engine from
scratch - its own bytecode, palettes, polygons, sounds and music - in the
layout of the release asked for (--release), which exercises the path that
release's real files take through the core:

  dos      MEMLIST.BIN and banks; the sound a raw sample (0x33, which the mt32
           setting plays on a CM-32L instead), the music a module whose
           patterns tell the script their row (VAR_MUSIC_SYNC)
  15th     the 15th Anniversary Edition's Data/Pak01.pak; WAV sounds (and
           their remastered versions, rmsnd/), the intro's music a WAV
           (Music/AW/Intro2004.wav, and Music/AW/RmSnd/)
  20th     the 20th Anniversary Edition's game/ folder: game/DAT, gzip'd WAV
           sounds (game/WGZ, and game/WGZ/original), Ogg Vorbis music
           (game/OGG: the intro's, and an ambience that loops; tests/tune.ogg)
  win31    Windows 3.1's BANK (its entries' table enciphered as rawgl reads
           it, the entries as unpacked files beside it, which rawgl takes
           first), its palette format, a WAV sound, the intro's music a MIDI
           file (played with the SoundFont the gate puts beside the zip)
  3do      the 3DO's GameData folder: little-endian bytecode with the 3DO's
           own opcodes, its shapes and 15-bit palettes, the logo and title
           pictures the engine shows before the game, an AIFF sound the game
           preloads, SDX2-compressed AIFF-C songs, and the 320x240 picture the
           pause shows
  3do-iso  the same, as an Opera disc image (.iso), given as it is
  3do-chd  the same disc as MAME's compressed image (.chd, stored uncompressed:
           one MODE1_RAW track, as a real 3DO disc's), given as it is

The parts (the 3DO starts at its logos and title, the anniversary editions at
16001; DOS and Windows 3.1, having a password screen, at 16000):

  16000 - a square crossing the screen every 40 ms; the fire button goes on.
  16001 - 25 frames: the music starts (a module's patterns tell the script
          their row through VAR_MUSIC_SYNC, and a marker shows it), then the
          game goes to 16003 by itself.
  16003 - the joystick moves a square (the 3DO's jump moves it up), fire plays
          a sound, a marker sits where the random seed (the randomSeed
          setting) puts it, another follows the music; P pauses (the engine's
          pause), C goes to the password screen.
  16008 - the password screen: a letter typed puts a square at its place in
          the alphabet; fire goes back to 16003.

The files go into a zip (except 3do-iso's image) the way a person would make
one - in a folder, the names in capitals where the release has them.

--container puts the DOS files on floppy disks' images instead of a zip, the
formats the core reads directly: img (a 1.44 MB DOS disk), adf (two Amiga OFS
disks), st, msa (packed) and stx (Pasti, a protected track and all) - the Atari
ST's, two disks each - and zip-adf (a zip holding the two .adf); the files are
split across the two disks with one on both, and <out> is a folder, which gets
the images and the slot map naming them.

Two broken variants (DOS) test what a fault in the game's data does to the
machine: --bad-opcode ends the intro on an opcode rawgl does not have (its
error()), --bad-shape draws a polygon of more vertices than rawgl allows (its
assertion). Either halts the machine; the gate checks it keeps stepping.

usage: make-synthetic.py [--release dos|15th|20th|win31|3do|3do-iso|3do-chd]
                         [--container img|adf|st|msa|stx|zip-adf] [--bad-opcode | --bad-shape] <out>
"""

import gzip
import os
import re
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RAWGL = os.path.join(HERE, "..", "..", "extern", "rawgl")

RT_SOUND, RT_MUSIC, RT_BITMAP, RT_PALETTE, RT_BYTECODE, RT_SHAPE = 0, 1, 2, 3, 4, 5

VAR_RANDOM_SEED = 0x3C
VAR_LAST_KEYCHAR = 0xDA
VAR_MUSIC_SYNC = 0xF4
VAR_HERO_ACTION = 0xFA
VAR_HERO_POS_JUMP_DOWN = 0xFB
VAR_HERO_POS_LEFT_RIGHT = 0xFC
VAR_PAUSE_SLICES = 0xFF

RELEASE = "dos"
BROKEN = None


class Asm:
    """rawgl's opcodes (staticres.cpp Script::_opTable), with labels."""

    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []
        # the 3DO's bytecode: little-endian words, and its own opcodes for the
        # palette (11), the shifts (22) and the music (26) (Script::executeTask)
        self.le = RELEASE.startswith("3do")

    def label(self, name):
        self.labels[name] = len(self.code)

    def _b(self, *bs):
        self.code += bytes(b & 0xFF for b in bs)

    def _w(self, w):
        self.code += struct.pack("<H" if self.le else ">H", w & 0xFFFF)

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

    def set_palette(self, num):
        if self.le: self._b(0x0B, num)        # 3DO op11: the palette, now
        else: self._b(0x0B, num, 0)           # the word's high byte is the palette

    def select_page(self, p): self._b(0x0D, p)
    def fill_page(self, p, color): self._b(0x0E, p, color)
    def update_display(self, p): self._b(0x10, p)
    def and_(self, var, n): self._b(0x14, var); self._w(n)

    def shl(self, var, n):
        if self.le: self._b(0x16, var, n)     # 3DO op22: a byte
        else: self._b(0x16, var); self._w(n)

    def play_sound(self, res, freq, vol, channel): self._b(0x18); self._w(res); self._b(freq, vol, channel)
    def update_resources(self, num): self._b(0x19); self._w(num)

    def play_music(self, res, delay, pos):
        if self.le: self._b(0x1A, res)        # 3DO op26: song <res>
        else: self._b(0x1A); self._w(res); self._w(delay); self._b(pos)

    def draw_at_vars(self, shape_offset, xvar, yvar):
        # 0x40 | 0x10 (x from a variable) | 0x04 (y from a variable), zoom 64;
        # the offset is read a byte at a time, high first, on every release
        assert shape_offset % 2 == 0
        self._b(0x54, shape_offset // 2 >> 8, shape_offset // 2, xvar, yvar)

    def build(self):
        for at, name in self.fixups:
            struct.pack_into("<H" if self.le else ">H", self.code, at, self.labels[name])
        return bytes(self.code)


# ------------------------------------------------------------ pictures

SHAPE_BIG, SHAPE_SMALL, SHAPE_BAD = 0, 16, 32


def shapes():
    if RELEASE.startswith("3do"):
        # the 3DO's rectangle (Video::drawShape3DO, code 0x20 | colour)
        data = bytearray([0x20 | 4, 20, 20])
        data += bytes(SHAPE_SMALL - len(data))
        data += bytes([0x20 | 5, 8, 8])
        return bytes(data)

    def rect(color, w, h):
        return bytes([0xC0 | color, w, h, 4, w, 0, w, h, 0, h, 0, 0])
    data = bytearray(rect(4, 20, 20))
    data += bytes(SHAPE_SMALL - len(data))
    data += rect(5, 8, 8)
    data += bytes(SHAPE_BAD - len(data))
    # 70 vertices: one more than rawgl's QuadStrip holds (MAX_VERTICES)
    data += bytes([0xC0 | 6, 40, 40, 70]) + bytes(140)
    return bytes(data)


BASE_COLOURS = [0x000, 0xF00, 0x0F0, 0x00F, 0xFFF, 0xFF0, 0x0FF, 0xF0F,
                0x888, 0x800, 0x080, 0x008, 0x444, 0x880, 0x088, 0x808]


def palette_colours(p):
    cols = list(BASE_COLOURS)
    cols[0] = [0x000, 0x224, 0x420, 0x042][p % 4]   # each part's background
    return cols


def palettes():
    out = bytearray()
    if RELEASE == "win31":
        # rawgl readPaletteWin31: 32 palettes of 16 indices, then (at 0xC04)
        # the colours they index, 0x00BBGGRR
        table = bytearray()
        for p in range(32):
            for i, c in enumerate(palette_colours(p)):
                out += struct.pack("<H", p * 16 + i)
                r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
                table += struct.pack("<I", (r * 17) | (g * 17) << 8 | (b * 17) << 16)
        out += bytes(0xC04 - len(out))
        return bytes(out + table)
    if RELEASE.startswith("3do"):
        # rawgl readPalette3DO: 16 big-endian 15-bit colours a palette
        for p in range(32):
            for c in palette_colours(p):
                r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
                out += struct.pack(">H", (r * 2) << 10 | (g * 2) << 5 | (b * 2))
        return bytes(out)
    # 32 palettes of 16 0x0RGB words (the VGA half), then the EGA half
    for p in range(32):
        for c in palette_colours(p):
            out += struct.pack(">H", c)
    return bytes(out) + bytes(1024)


def bitmap_3do(num):
    """a 320x200 15-bit picture, big-endian, two rows interleaved
    (rawgl video.cpp deinterlace555): a checkerboard, a colour per picture"""
    colour = [0x7C00, 0x03E0, 0x001F, 0x7FE0][num % 4]
    out = bytearray()
    for y in range(0, 200, 2):
        for x in range(320):
            for yy in (y, y + 1):
                out += struct.pack(">H", colour if ((x // 20) + (yy // 20)) % 2 else 0x0421)
    return bytes(out)


def ccb_3do(w, h):
    """a 16-bit coded cel as rawgl reads one (resource_3do.cpp decodeShapeCcb):
    each row a run of one colour per 64 pixels, a gradient down the picture"""
    pre0 = 6 | (h - 1) << 6                  # 16 bits per pixel, the height
    pre1 = w - 1
    out = bytearray(struct.pack(">II", 1 << 9, 0) + struct.pack(">I", 0x30) + bytes(40) + struct.pack(">II", pre0, pre1))
    for y in range(h):
        packets = bytearray()
        left = w
        while left > 0:
            n = min(64, left)
            packets += bytes([0xC0 | (n - 1)]) + struct.pack(">H", ((y * 31 // h) << 10) | 0x10)
            left -= n
        size = 2 + len(packets)
        size4 = (size + 3) // 4 * 4
        out += struct.pack(">H", size4 // 4 - 2) + packets + bytes(size4 - size)
    return bytes(out)


# ------------------------------------------------------------ sounds

def square(n, period, amp):
    return [amp if (i // period) % 2 else -amp for i in range(n)]


def wav(samples, rate, channels=1):
    """RIFF WAVE, 16-bit PCM"""
    data = b"".join(struct.pack("<h", s) for s in samples)
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * 2 * channels, 2 * channels, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def ext80(rate):
    """a rate as an 80-bit extended float"""
    k = rate.bit_length() - 1
    return struct.pack(">HI", 16383 + k, (rate << (31 - k)) & 0xFFFFFFFF) + bytes(4)


def aiff(samples, rate):
    """FORM AIFF, 16-bit mono, as SDL_mixer (and the core) loads one"""
    data = b"".join(struct.pack(">h", s) for s in samples)
    comm = struct.pack(">HIH", 1, len(samples), 16) + ext80(rate)
    ssnd = struct.pack(">II", 0, 0) + data
    body = b"AIFF" + b"COMM" + struct.pack(">I", len(comm)) + comm + b"SSND" + struct.pack(">I", len(ssnd)) + ssnd
    return b"FORM" + struct.pack(">I", len(body)) + body


def aifc_sdx2(frames, period):
    """FORM AIFF-C, stereo SDX2 (rawgl aifcplayer.cpp): a byte d decodes to
    2*d*|d| (d even: no delta), so +-64 is a square wave of +-8192"""
    data = bytearray()
    for i in range(frames):
        d = 64 if (i // period) % 2 else -64
        data += struct.pack("bb", d, -d)
    comm = struct.pack(">HIH", 2, frames, 16) + ext80(22050) + b"SDX2" + b"\x04SDX2\x00"
    ssnd = struct.pack(">II", 0, 0) + bytes(data)
    body = (b"AIFC" + b"FVER" + struct.pack(">II", 4, 0xA2805140) + b"COMM" + struct.pack(">I", len(comm)) + comm
            + b"SSND" + struct.pack(">I", len(ssnd)) + ssnd)
    return b"FORM" + struct.pack(">I", len(body)) + body


def sound_raw():
    # a raw sound: length / 2, loop length / 2, 4 unused bytes, signed 8-bit
    n = 2000
    samples = bytes((0x40 if (i // 25) % 2 else 0xC0) for i in range(n))
    return struct.pack(">HH", n // 2, 0) + bytes(4) + samples


def music_module():
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


def midi_tune():
    """a type-0 MIDI file: eight piano notes"""
    def vlq(n):
        b = [n & 0x7F]
        n >>= 7
        while n:
            b.append((n & 0x7F) | 0x80)
            n >>= 7
        return bytes(reversed(b))
    ev = bytearray(vlq(0) + bytes([0xC0, 0]))
    for n in [60, 64, 67, 72, 67, 64, 60, 55]:
        ev += vlq(0) + bytes([0x90, n, 100]) + vlq(64) + bytes([0x80, n, 0])
    ev += vlq(0) + bytes([0xFF, 0x2F, 0])
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 192) + b"MTrk" + struct.pack(">I", len(ev)) + bytes(ev)


# ------------------------------------------------------------ the parts

# what the parts play, per release: (resources to load, the intro's music,
# the play part's music, the sound fire plays)
def audio_plan():
    if RELEASE == "dos":
        # fire's sound is 0x33, one of the numbers rawgl plays on a CM-32L
        # with the mt32 setting (Mixer::_mt32SoundsTable: a rhythm note)
        return [0x01, 0x02, 0x33], 0x02, 0x02, 0x33
    if RELEASE in ("15th", "win31"):
        return [], 7, None, 0x01          # the intro's music, by its number (7)
    if RELEASE == "20th":
        return [], 7, 5001, 0x01          # and an ambience (5001..5010 loop)
    return [2001], None, 2, 0x33          # 3DO: preload list 2001 (sound 0x33), song 2


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


def part_intro():
    a = Asm()
    a.set_palette(1)
    loads, intro_music, _, _ = audio_plan()
    for n in loads:
        a.update_resources(n)
    if intro_music is not None:
        a.play_music(intro_music, 0, 0)
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
    loads, _, play_music, fire_sound = audio_plan()
    for n in loads:
        a.update_resources(n)
    if play_music is not None:
        a.play_music(play_music, 0, 0)
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
    a.play_sound(fire_sound, 20, 63, 0)
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


def resources():
    """the parts' resources by number (rawgl Resource::_memListParts)"""
    pal, shp = palettes(), shapes()
    return {
        0x11: (RT_SHAPE, shp),
        0x14: (RT_PALETTE, pal), 0x15: (RT_BYTECODE, part_protection()), 0x16: (RT_SHAPE, shp),
        0x17: (RT_PALETTE, pal), 0x18: (RT_BYTECODE, part_intro()), 0x19: (RT_SHAPE, shp),
        0x1D: (RT_PALETTE, pal), 0x1E: (RT_BYTECODE, part_play()), 0x1F: (RT_SHAPE, shp),
        0x7D: (RT_PALETTE, pal), 0x7E: (RT_BYTECODE, part_password()), 0x7F: (RT_SHAPE, shp),
    }


# ------------------------------------------------------------ the releases

def build_dos():
    res = {n: (t, d, 1) for n, (t, d) in resources().items()}
    res[0x01] = (RT_SOUND, sound_raw(), 1)
    res[0x02] = (RT_MUSIC, music_module(), 1)
    res[0x33] = (RT_SOUND, sound_raw(), 1)
    # the password screen's code in a bank of its own: its being there is what
    # tells the engine the release has a password screen (and so a copy
    # protection to start at)
    res[0x7E] = (res[0x7E][0], res[0x7E][1], 0x0D)
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


def pak(entries):
    """the 15th Anniversary Edition's Pak01.pak (rawgl pak.cpp): "PACK", the
    table's offset and size, then 0x40-byte entries named dlx/<name>"""
    data = bytearray(12)
    table = bytearray()
    for name, body in sorted(entries.items()):
        off = len(data)
        data += body
        rec = ("dlx/" + name).encode().ljust(0x38, b"\0") + struct.pack("<II", off, len(body))
        table += rec
    struct.pack_into("<4sII", data, 0, b"PACK", len(data), len(table))
    return bytes(data + table)


def build_15th():
    entries = {"file%03d.dat" % n: d for n, (t, d) in resources().items()}
    entries["file001.wav"] = wav(square(4000, 30, 8000), 22050)
    entries["rmsnd/file001.wav"] = wav(square(4000, 15, 8000), 22050)
    return {
        "Data/Pak01.pak": pak(entries),
        "Music/AW/Intro2004.wav": wav([s for s in square(88200, 100, 6000) for _ in (0, 1)], 44100, 2),
        "Music/AW/RmSnd/Intro2004.wav": wav([s for s in square(88200, 50, 6000) for _ in (0, 1)], 44100, 2),
        "Menu/lang_English.Txt": b"000 SYNTHETIC\r\n",
    }


def build_20th():
    files = {"game/DAT/FILE%03d.DAT" % n: d for n, (t, d) in resources().items()}
    tune = open(os.path.join(HERE, "tune.ogg"), "rb").read()
    gz = lambda b: gzip.compress(b, mtime=0)
    files.update({
        "game/WGZ/file001.wgz": gz(wav(square(4000, 15, 8000), 22050)),
        "game/WGZ/original/file001.wgz": gz(wav(square(4000, 30, 8000), 22050)),
        "game/OGG/Intro_20th.ogg": tune,
        "game/OGG/original/intro.ogg": tune,
        "game/OGG/amb5001.ogg": tune,
        "game/BGZ/Font.bgz": gz(bytes(64)),
        "game/TXT/EN.txt": b"SYNTHETIC\n",
    })
    return files


def win31_shuffle():
    """rawgl's own table, read out of resource_win31.cpp"""
    src = open(os.path.join(RAWGL, "resource_win31.cpp")).read()
    body = src[src.index("_shuffleTable[256]"):]
    body = body[body.index("{") + 1:body.index("}")]
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]


def win31_cipher(buf, key, table):
    """rawgl resource_win31.cpp decode(): a key stream independent of the data,
    so enciphering is the same XOR"""
    out = bytearray(buf)
    for i in range(len(out)):
        dl = (1 + (key >> 8)) & 0xFF
        al = table[dl]
        dh = al ^ (key & 0xFF)
        out[i] ^= al
        key = (dh << 8) | dl
    return bytes(out), key


def build_win31():
    table = win31_shuffle()
    res = {n: d for n, (t, d) in resources().items()}
    res[0x01] = wav(square(4000, 30, 8000), 22050)
    res[148] = struct.pack("<I", 0xFFFF0000)        # the strings: none
    count = 149
    key = 0x1234
    header = bytearray(32)
    header[0:4] = b"NL\0\0"
    struct.pack_into("<H", header, 4, count)
    struct.pack_into("<H", header, 0x14, key)
    files = {}
    table_bytes = bytearray()
    for num in range(count):
        name = ("R%03d" % num).encode() if num in res else b""
        size = len(res[num]) if num in res else 0
        rec = bytearray(32)
        rec[0:16] = name.ljust(16, b"\0")
        struct.pack_into("<H", rec, 16, 0x80 if size else 0)
        struct.pack_into("<III", rec, 20, size, 0, 0)
        enc, key = win31_cipher(bytes(rec), key, table)
        table_bytes += enc
        if num in res:
            files["%03d_R%03d" % (num, num)] = res[num]
    files["BANK"] = bytes(header + table_bytes)
    files["y.mid"] = midi_tune()
    return files


def build_3do():
    files = {"GameData/File%d" % n: d for n, (t, d) in resources().items()}
    files["GameData/File51"] = aiff(square(4000, 20, 8000), 22050)       # sound 0x33
    for n in (67, 68, 69, 70):
        files["GameData/File%d" % n] = bitmap_3do(n)
    files["GameData/File340"] = bytes(16)                                  # what the release is known by
    files["GameData/song1"] = aifc_sdx2(22050, 50)
    files["GameData/song2"] = aifc_sdx2(22050, 25)
    files["GameData/PauseShape"] = ccb_3do(320, 240)
    return files


def opera_iso(files):
    """a 3DO disc (rawgl resource_3do.cpp OperaIso): the volume header, a root
    directory holding GameData, GameData's directory, then the files, each in
    whole 2048-byte blocks; a directory entry is 72 bytes, the last of a block
    flagged 0x40, the last of the directory 0x80"""
    B = 2048
    names = sorted(n.split("/", 1)[1] for n in files)
    per_block = (B - 20) // 72
    dir_blocks = (len(names) + per_block - 1) // per_block
    first_file = 2 + dir_blocks
    blocks = {}
    at = first_file
    for n in names:
        blocks[n] = at
        at += (len(files["GameData/" + n]) + B - 1) // B
    image = bytearray(at * B)
    image[0] = 1
    image[40:46] = b"CD-ROM"
    struct.pack_into(">I", image, 100, 1)

    def entry(attr, size, name, block):
        rec = bytearray(72)
        struct.pack_into(">I", rec, 0, attr)
        struct.pack_into(">I", rec, 16, size)
        rec[32:32 + len(name)] = name.encode()
        struct.pack_into(">II", rec, 64, 0, block)
        return rec

    image[B + 20:B + 20 + 72] = entry(0x80000007, 0, "GameData", 2)
    for i, n in enumerate(names):
        blk, slot = divmod(i, per_block)
        flags = 0
        if i == len(names) - 1:
            flags = 0x80000000
        elif slot == per_block - 1:
            flags = 0x40000000
        off = (2 + blk) * B + 20 + slot * 72
        image[off:off + 72] = entry(flags | 2, len(files["GameData/" + n]), n, blocks[n])
        body = files["GameData/" + n]
        image[blocks[n] * B:blocks[n] * B + len(body)] = body
    return bytes(image)


def chd_of_image(image):
    """a CHD v5 of a CD holding the image as one MODE1_RAW track, its hunks
    stored uncompressed (libchdr's hunk_read_uncompressed): the header, the
    track's metadata (CHT2), the map (each hunk's place, in hunks), the hunks;
    a frame is 2352 bytes of sector - sync, header, the 2048 bytes, an EDC/ECC
    left zero (libchdr checks it only on compressed hunks) - and 96 of subcode"""
    FRAME, FPH = 2448, 8
    sectors = (len(image) + 2047) // 2048
    frames = (sectors + 3) // 4 * 4                      # chdman pads a track to 4 frames
    hunkbytes = FRAME * FPH
    hunks = (frames + FPH - 1) // FPH
    meta_text = ("TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:%d PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0" % frames).encode() + b"\0"
    meta_off = 124
    meta = struct.pack(">4sIQ", b"CHT2", (1 << 24) | len(meta_text), 0) + meta_text
    map_off = meta_off + len(meta)
    first_hunk = (map_off + hunks * 4 + hunkbytes - 1) // hunkbytes
    header = bytearray(124)
    struct.pack_into(">8sII", header, 0, b"MComprHD", 124, 5)
    struct.pack_into(">QQQII", header, 32, frames * FRAME, map_off, meta_off, hunkbytes, FRAME)
    out = bytearray(first_hunk * hunkbytes)
    out[0:124] = header
    out[meta_off:meta_off + len(meta)] = meta
    for h in range(hunks):
        struct.pack_into(">I", out, map_off + h * 4, first_hunk + h)
    body = bytearray(hunks * hunkbytes)
    for s in range(frames):
        f = bytearray(FRAME)
        f[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
        m, sec = divmod(s + 150, 75)
        mn, sc = divmod(m, 60)
        f[12:16] = bytes([int(str(mn), 16), int(str(sc), 16), int(str(sec), 16), 1])   # BCD minute, second, frame; mode 1
        f[16:16 + 2048] = image[s * 2048:(s + 1) * 2048].ljust(2048, b"\0")
        body[s * FRAME:(s + 1) * FRAME] = f
    return bytes(out + body)



# ------------------------------------------------------------ floppy disks

def fat_image(files, total, spc, nroot, spf, spt, heads, media):
    """a FAT12 disk (DOS; the Atari ST's are the same file system), the files
    in its root, each in consecutive clusters"""
    img = bytearray(total * 512)
    boot = bytearray(512)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"SYNTH   "
    struct.pack_into("<HBHBHHBHHH", boot, 11, 512, spc, 1, 2, nroot, total, media, spf, spt, heads)
    img[0:512] = boot
    fat = [0] * (total // spc + 2)
    fat[0], fat[1] = 0xF00 | media, 0xFFF
    root_off = (1 + 2 * spf) * 512
    data_off = root_off + nroot * 32
    cb = spc * 512
    nxt = 2
    for i, (name, body) in enumerate(sorted(files.items())):
        n = max(1, (len(body) + cb - 1) // cb)
        first = nxt
        for k in range(n):
            fat[nxt + k] = (nxt + k + 1) if k < n - 1 else 0xFFF
        nxt += n
        o = data_off + (first - 2) * cb
        img[o:o + len(body)] = body
        base, _, ext = name.partition(".")
        e = bytearray(32)
        e[0:8] = base.upper().ljust(8).encode()
        e[8:11] = ext.upper().ljust(3).encode()
        e[11] = 0x20
        struct.pack_into("<HI", e, 26, first, len(body))
        img[root_off + i * 32:root_off + i * 32 + 32] = e
    tab = bytearray(spf * 512)
    for c in range(len(fat)):
        o = c * 3 // 2
        if o + 1 >= len(tab): break
        v = fat[c] & 0xFFF
        if c & 1:
            tab[o] = (tab[o] & 0x0F) | ((v << 4) & 0xF0); tab[o + 1] = v >> 4
        else:
            tab[o] = v & 0xFF; tab[o + 1] = (tab[o + 1] & 0xF0) | (v >> 8)
    for k in range(2):
        img[512 + k * spf * 512:512 + (k + 1) * spf * 512] = tab
    return bytes(img)


def st_disk(files):
    # an Atari ST double-sided disk: 80 tracks, 2 sides, 9 sectors
    return fat_image(files, 1440, 2, 112, 3, 9, 2, 0xF9)


def msa(raw, spt=9, sides=2):
    """MSA: the tracks one by one, each packed (0xE5 runs) when that is shorter"""
    tracks = len(raw) // (spt * 512 * sides)
    out = bytearray(struct.pack(">HHHHH", 0x0E0F, spt, sides - 1, 0, tracks - 1))
    for t in range(tracks * sides):
        tr = raw[t * spt * 512:(t + 1) * spt * 512]
        packed = bytearray()
        i = 0
        while i < len(tr):
            j = i
            while j < len(tr) and tr[j] == tr[i] and j - i < 0xFFFF: j += 1
            if j - i >= 5 or tr[i] == 0xE5:
                packed += bytes([0xE5, tr[i]]) + struct.pack(">H", j - i); i = j
            else:
                packed.append(tr[i]); i += 1
        body = packed if len(packed) < len(tr) else tr
        out += struct.pack(">H", len(body)) + body
    return bytes(out)


def stx(raw, spt=9, sides=2):
    """Pasti: a record a track, its sectors described; and past the file
    system a protected track - an 1024-byte sector, a sector of another
    track's number, a sector without data - which the core leaves alone"""
    tracks = len(raw) // (spt * 512 * sides)
    recs = []
    for c in range(tracks + 2):
        for h in range(sides):
            descs, data = bytearray(), bytearray()
            if c < tracks:
                for sct in range(spt):
                    o = ((c * sides + h) * spt + sct) * 512
                    descs += struct.pack("<IHHBBBBHBB", len(data), 0, 0, c, h, sct + 1, 2, 0, 0, 0)
                    data += raw[o:o + 512]
            else:
                for sct, (idt, size, fdc) in enumerate([(c, 3, 0), (c + 5, 2, 0), (c, 2, 0x10)]):
                    descs += struct.pack("<IHHBBBBHBB", len(data), 0, 0, idt, h, sct + 1, size, 0, fdc, 0)
                    if not fdc & 0x10: data += bytes([0x5A]) * (128 << size)
            n = len(descs) // 16
            recs.append(struct.pack("<IIHHHBB", 16 + len(descs) + len(data), 0, n, 0x01, 6250, c | (h << 7), 0) + descs + data)
    return b"RSY\0" + struct.pack("<HHHBBI", 3, 1, 0, len(recs), 2, 0) + b"".join(recs)


def adf(files, label):
    """an Amiga OFS disk: the boot block, the root at block 880, a header
    block and OFS data blocks a file; hash chains as AmigaDOS makes them"""
    B, N = 512, 1760
    blocks = [bytearray(B) for _ in range(N)]
    blocks[0][0:4] = b"DOS\0"
    root = blocks[880]
    def name_to(b, name):
        b[B - 80] = len(name); b[B - 79:B - 79 + len(name)] = name.encode()
    def hashname(name):
        h = len(name)
        for ch in name.upper(): h = (h * 13 + ord(ch)) & 0x7FF
        return h % 72
    def checksum(b, at=20):
        struct.pack_into(">I", b, at, 0)
        struct.pack_into(">I", b, at, (-sum(struct.unpack(">128I", bytes(b)))) & 0xFFFFFFFF)
    struct.pack_into(">III", root, 0, 2, 0, 0)
    struct.pack_into(">I", root, 12, 72)
    struct.pack_into(">i", root, B - 4, 1)
    name_to(root, label)
    free = 882
    for name, body in sorted(files.items()):
        hdr_n = free; free += 1
        hdr = blocks[hdr_n]
        chunks = [body[i:i + 488] for i in range(0, len(body), 488)] or [b""]
        assert len(chunks) <= 72, "one header block only"
        data_ns = list(range(free, free + len(chunks))); free += len(chunks)
        struct.pack_into(">IIII", hdr, 0, 2, hdr_n, len(chunks), 0)
        struct.pack_into(">I", hdr, 16, data_ns[0])
        for k, dn in enumerate(data_ns):
            struct.pack_into(">I", hdr, B - 204 - k * 4, dn)
            d = blocks[dn]
            struct.pack_into(">IIIII", d, 0, 8, hdr_n, k + 1, len(chunks[k]), data_ns[k + 1] if k + 1 < len(data_ns) else 0)
            d[24:24 + len(chunks[k])] = chunks[k]
            checksum(d)
        struct.pack_into(">I", hdr, B - 188, len(body))
        name_to(hdr, name)
        struct.pack_into(">I", hdr, B - 12, 880)
        struct.pack_into(">i", hdr, B - 4, -3)
        slot = 24 + hashname(name) * 4
        struct.pack_into(">I", hdr, B - 16, struct.unpack_from(">I", root, slot)[0])
        struct.pack_into(">I", root, slot, hdr_n)
        checksum(hdr)
    checksum(root)
    return b"".join(bytes(b) for b in blocks)


def containers(kind, files, out):
    """the DOS files on floppy images in folder `out`, and its slot map"""
    import json
    os.makedirs(out, exist_ok=True)
    names = sorted(files)
    half = len(names) // 2
    d1 = {n: files[n] for n in names[:half + 1]}           # one file on both disks
    d2 = {n: files[n] for n in names[half:]}
    if kind == "img":
        disks = {"DISK1.IMG": fat_image(files, 2880, 1, 224, 9, 18, 2, 0xF0)}
    elif kind == "adf" or kind == "zip-adf":
        disks = {"Disk1.adf": adf(d1, "Disk1"), "Disk2.adf": adf(d2, "Disk2")}
    elif kind == "st":
        disks = {"DISK1.ST": st_disk(d1), "DISK2.ST": st_disk(d2)}
    elif kind == "msa":
        disks = {"DISK1.MSA": msa(st_disk(d1)), "DISK2.MSA": msa(st_disk(d2))}
    elif kind == "stx":
        disks = {"DISK1.STX": stx(st_disk(d1)), "DISK2.STX": stx(st_disk(d2))}
    else:
        sys.exit("no such container: " + kind)
    if kind == "zip-adf":
        with zipfile.ZipFile(os.path.join(out, "disks.zip"), "w", zipfile.ZIP_DEFLATED) as z:
            for n, d in disks.items(): z.writestr(n, d)
        listed = ["disks.zip"]
    else:
        for n, d in disks.items(): open(os.path.join(out, n), "wb").write(d)
        listed = sorted(disks)
    json.dump({"game": listed}, open(os.path.join(out, "slots"), "w"))

BUILDERS = {"dos": build_dos, "15th": build_15th, "20th": build_20th, "win31": build_win31,
            "3do": build_3do, "3do-iso": build_3do, "3do-chd": build_3do}


def main():
    global RELEASE, BROKEN
    args = sys.argv[1:]
    container = None
    while args and args[0].startswith("--"):
        opt = args.pop(0)
        if opt == "--container" and args:
            container = args.pop(0)
        elif opt == "--release" and args:
            RELEASE = args.pop(0)
            if RELEASE not in BUILDERS:
                sys.exit("no such release: " + RELEASE)
        elif opt in ("--bad-opcode", "--bad-shape"):
            BROKEN = opt[6:]
        else:
            sys.exit(__doc__)
    if len(args) != 1:
        sys.exit(__doc__)
    files = BUILDERS[RELEASE]()
    if container:
        containers(container, files, args[0])
        return
    if RELEASE == "3do-iso":
        open(args[0], "wb").write(opera_iso(files))
        return
    if RELEASE == "3do-chd":
        open(args[0], "wb").write(chd_of_image(opera_iso(files)))
        return
    with zipfile.ZipFile(args[0], "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo("SYNTH/", date_time=(1991, 1, 1, 0, 0, 0)), b"")
        for name in sorted(files):
            info = zipfile.ZipInfo("SYNTH/" + name, date_time=(1991, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, files[name])
        z.writestr(zipfile.ZipInfo("README.TXT", date_time=(1991, 1, 1, 0, 0, 0)), b"the core's synthetic test game\n")


if __name__ == "__main__":
    main()
