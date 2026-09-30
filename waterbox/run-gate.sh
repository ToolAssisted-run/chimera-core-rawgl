#!/bin/sh
# run-gate.sh - the rawgl core's gate: the native reference and core.wbx are
# the same machine, on the core's own synthetic game (tests/make-synthetic.py)
# and, when one is given, on Another World itself.
#
# Every leg says what it compared. The legs:
#   equivalence  run-native and run-wbx: every step's picture, sound, length
#                and lag, the machine's clock and both memory domains
#   rerecord     the sandbox saved and loaded before every step = without
#   session      saved at a step, a new host, loaded, finished = without
#   turbo        the first half not drawn: the rest the same, and the first
#                half's pictures really not drawn
#   releases     the synthetic game as the 15th and 20th Anniversary Editions,
#                Windows 3.1 and the 3DO (its folder, its disc image read in
#                place, and the disc as a CHD): native = sandbox, rerecord,
#                session, each; the 3DO's folder, image and CHD the same
#                machine; Jump the 3DO's only; the soundFont setting (with the
#                SoundFont firmware) changes Windows 3.1's sound and nothing else
#   disks        the synthetic DOS game on floppy images the core reads as they
#                are, as the releases' firmware - a DOS .img (dos-disk), two
#                Amiga .adf (amiga-en-disk1, 2), two Atari ST .st, .msa and .stx
#                (a protected track and all; atari-disk1, 2), each .adf in a zip
#                of its own - each the same machine as the zip, native = sandbox
#   loose        the synthetic 15th Anniversary Edition's files as they come out
#                of its installer, loose (its Pak01.pak under another name, the
#                intro's music an Ogg, its texts) = the same files zipped in
#                their folders, native = sandbox, session
#   settings     randomSeed is what the script's seed starts at, language what
#                the DOS copy protection's title choice reads, difficulty and
#                remasteredAudio what the anniversary editions read (both
#                builds)
#   firmware     waterbox.config's machines and firmware = the loader's tables;
#                the zip as dos-disk = game.zip; a setting left on from another
#                release (mt32 on the Amiga) asks for nothing
#   slots        a slot map names the zip (a host that is no project's)
#   mt32         the DOS release's sound effects on a CM-32L (Munt), with the
#                ROMs -r names: native = sandbox, rerecord, session, and only
#                the sound changes; without ROMs, or with a file that is not
#                one, a refusal that names it
#   refusals     no zip, not a zip, no game in it, data rawgl cannot tell, a
#                15th Anniversary Edition's Pak01.pak that is not one, a seed
#                out of range, an installer (NSIS, loose; Inno Setup, in a
#                zip), a PC CD (.iso, .chd), the soundFont setting without its
#                firmware or with a file that is not a SoundFont: each says why
#                - the installers and the PC CD naming the files to add instead
#   halts        rawgl's error() and a failed assertion halt the machine, which
#                keeps stepping; the same in both builds
#   clock        the guest has no time() of its own: the one it calls is the
#                core's (the setting), and nothing of the engine reads a clock
#   teeth        the equivalence comparison sees a one-step difference
#   engine       (with -c) the package through Chimera's own engine, headless:
#                chimera-run plays a movie in Chimera's format (console
#                buttons, then P1's), with and without rerecording
#
# usage: run-gate.sh [-m <miniBox dir>] [-g [<id>=]<file>]... [-R <release>] [-M <movie>]
#                    [-f <frames>] [-c <chimera-run>] [-r <ROM dir>]
#   -g adds the equivalence, rerecord and session legs on a real release: -g
#      <id>=<file> once for each of the release's files, as a project brings
#      them (its firmware under its id: dos-disk=..., amiga-fr-disk1=...), -R
#      the release (dos when omitted); a SoundFont (.sf2) for Windows 3.1's
#      music (the firmware, with the soundFont setting); a file without an id
#      goes in a "game" slot, as a host that is no project's may bring it;
#      not in the repo): 2000 steps from power-on, or the steps of the movie -M
#      names (a movie of your own, kept out of the repo)
#   -c runs the engine leg with that chimera-run (build/dll/chimera-run of a
#      Chimera checkout); it packages the core first
#   -r runs the MT-32 legs with the CM-32L's ROMs from that folder
#      (CM32L_CONTROL.ROM and CM32L_PCM.ROM, or Munt's names for them,
#      cm32l_ctrl_1_02.rom and cm32l_pcm.rom); never in the repo
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
game=""
frames=100
chimera_run=""
roms=""
game_movie=""
game_release=""
while getopts "m:g:f:c:r:M:R:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		g) game="${game:+$game
}$OPTARG" ;;
		f) frames="$OPTARG" ;;
		c) chimera_run="$OPTARG" ;;
		r) roms="$OPTARG" ;;
		M) game_movie="$OPTARG" ;;
		R) game_release="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" && pwd)"

echo "== build (miniBox $mb)"
mkdir -p "$root/build"
make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-native.log" 2>&1 || {
	tail -20 "$root/build/gate-native.log"; echo "native build failed"; exit 1; }
make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$root/build/gate-guest.log" 2>&1 || {
	tail -20 "$root/build/gate-guest.log"; echo "guest build failed"; exit 1; }
native="$root/build/native/run-native"
wbx="$root/build/native/run-wbx"
core="$root/build/guest/core.wbx"
movie="$here/tests/synthetic.movie"

work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"
fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# the digests only (miniBox's own log lines go to the same stdout)
digests() { grep -E '^[A-Za-z]+(\[[^]]*\])?=' "$1" || true; }
same() { # same <leg> <a> <b> <what>
	digests "$2" > "$2.d"
	digests "$3" > "$3.d"
	if cmp -s "$2.d" "$3.d"; then
		pass "$1: $4"
	else
		fail "$1: $4"; diff "$2.d" "$3.d" | head -20
	fi
}
value() { digests "$1" | sed -n "s/^$2=//p"; }
# the last traced value of a property (a trace line: step rate read values...)
last() { tail -1 "$1" | awk -v col="$2" '{print $(3 + col)}'; }

mkgame() { # mkgame <dir> [make-synthetic options]
	d="$work/$1"; shift
	mkdir -p "$d"
	python3 "$here/tests/make-synthetic.py" "$@" "$d/game.zip"
}

mkgame synth
echo "== the synthetic game ($frames steps, $movie)"
"$native" "$work/synth" --frames "$frames" --movie "$movie" > "$work/n.txt"
"$wbx" "$core" "$work/synth" --frames "$frames" --movie "$movie" > "$work/w.txt" 2> "$work/w.err"
same equivalence "$work/n.txt" "$work/w.txt" "native = sandbox, $frames steps through the four parts (clock $(value "$work/n.txt" clock) ms, lag $(value "$work/n.txt" lagFrames))"
"$wbx" "$core" "$work/synth" --frames "$frames" --movie "$movie" --rerecord > "$work/r.txt" 2> "$work/r.err"
same rerecord "$work/r.txt" "$work/w.txt" "save+load before every step = straight ($(sed -n 's/stateBytes=//p' "$work/r.err") state bytes)"
at=$((frames * 3 / 5))
"$wbx" "$core" "$work/synth" --frames "$frames" --movie "$movie" --session-at "$at" > "$work/s.txt" 2> "$work/s.err"
same session "$work/s.txt" "$work/w.txt" "saved at step $at (paused), loaded into a new host = straight"

"$native" "$work/synth" --frames "$frames" --movie "$movie" --turbo > "$work/t.txt"
digests "$work/t.txt" | grep -v '^videoHash=' > "$work/t.d"
digests "$work/n.txt" | grep -v '^videoHash=' > "$work/n.d"
if cmp -s "$work/t.d" "$work/n.d" && [ "$(value "$work/t.txt" videoHash)" != "$(value "$work/n.txt" videoHash)" ]; then
	pass "turbo: first half undrawn - second half's pictures, the sound, the steps and memory the same; the first half's pictures not"
else
	fail "turbo"; diff "$work/t.d" "$work/n.d" | head
fi

echo "== releases"
digest_run() { # digest_run <native|wbx> <dir> <frames> <movie> [options] > out
	b=$1; d=$2; n=$3; m=$4; shift 4
	if [ "$b" = native ]; then "$native" "$d" --frames "$n" --movie "$m" "$@" 2>/dev/null
	else "$wbx" "$core" "$d" --frames "$n" --movie "$m" "$@" 2>/dev/null; fi
}
for rel in 15th 20th win31 3do 3do-iso 3do-chd; do
	case $rel in
		15th|20th) m="$here/tests/synthetic-nth.movie"; n=90 ;;
		win31) m="$movie"; n=100 ;;
		*) m="$here/tests/synthetic-3do.movie"; n=110 ;;
	esac
	d="$work/rel-$rel"
	mkdir -p "$d"
	case $rel in
		# the 3DO's disc and the 20th's zip as a project brings them: the release's
		# firmware, under its id, and the release setting
		3do-iso) python3 "$here/tests/make-synthetic.py" --release $rel "$d/3do-disc"
			printf '{"release": "3do"}' > "$d/settings" ;;
		3do-chd) python3 "$here/tests/make-synthetic.py" --release $rel "$d/3do-disc"
			printf '{"release": "3do"}' > "$d/settings" ;;
		20th) python3 "$here/tests/make-synthetic.py" --release $rel "$d/20th-game"
			printf '{"release": "20th"}' > "$d/settings" ;;
		*) python3 "$here/tests/make-synthetic.py" --release $rel "$d/game.zip" ;;
	esac
	if [ $rel = win31 ]; then
		cp "$root/extern/TinySoundFont/examples/florestan-subset.sf2" "$d/soundfont.sf2"
		printf '{"release": "win31", "soundFont": true}' > "$d/settings"
	fi
	digest_run native "$d" $n "$m" --trace "$d/t" --trace-props "Game.Release,Game.Part" > "$d/n"
	digest_run wbx "$d" $n "$m" > "$d/w"
	digest_run wbx "$d" $n "$m" --rerecord > "$d/r"
	digest_run wbx "$d" $n "$m" --session-at $((n * 3 / 5)) > "$d/s"
	parts="$(awk 'NR > 1 { print $5 }' "$d/t" | uniq | tr '\n' ' ')"
	what="release $(awk 'NR == 2 { print $4 }' "$d/t"), parts ${parts% }"
	same "releases ($rel)" "$d/w" "$d/n" "native = sandbox, $n steps ($what)"
	same "releases ($rel)" "$d/r" "$d/w" "rerecord = straight"
	same "releases ($rel)" "$d/s" "$d/w" "session at step $((n * 3 / 5)) = straight"
done
same "releases (3do)" "$work/rel-3do-iso/n" "$work/rel-3do/n" "the disc image, read in place = the GameData folder"
same "releases (3do)" "$work/rel-3do-chd/n" "$work/rel-3do/n" "the disc as a CHD, read through libchdr = the GameData folder"
# the screen number before the script's first restartAt (the 3DO's logos):
# rawgl leaves it unset; the core gives it restartAt's -1, in both builds
sn_n="$("$native" "$work/rel-3do" --frames 2 --trace "$work/sn.n" --trace-props Game.Screen > /dev/null 2>&1; last "$work/sn.n" 1)"
sn_w="$("$wbx" "$core" "$work/rel-3do" --frames 2 --trace "$work/sn.w" --trace-props Game.Screen > /dev/null 2>&1; last "$work/sn.w" 1)"
if [ "$sn_n" = -1 ] && [ "$sn_w" = -1 ]; then
	pass "releases (3do): the screen number is -1 during the logos, in both builds"
else
	fail "releases (3do): the screen number during the logos is $sn_n native, $sn_w sandbox (want -1)"
fi
if [ "$(value "$work/rel-3do/n" activeButtons)" = 35 ] && [ "$(value "$work/rel-15th/n" activeButtons)" = 34 ]; then
	pass "releases: Jump is the 3DO's only (35 buttons there, 34 elsewhere)"
else
	fail "releases: active buttons 3DO $(value "$work/rel-3do/n" activeButtons), 15th $(value "$work/rel-15th/n" activeButtons)"
fi
# the soundFont setting off: the firmware beside the game is not used, the
# music is silent, and nothing else changes
mkdir -p "$work/rel-win31-nosf"
cp "$work/rel-win31/game.zip" "$work/rel-win31/soundfont.sf2" "$work/rel-win31-nosf/"
digest_run native "$work/rel-win31-nosf" 100 "$movie" > "$work/rel-win31-nosf/n"
grep -v '^audioHash=' "$work/rel-win31/n.d" > "$work/sf.a"
digests "$work/rel-win31-nosf/n" | grep -v '^audioHash=' > "$work/sf.b"
if cmp -s "$work/sf.a" "$work/sf.b" && [ "$(value "$work/rel-win31/n" audioHash)" != "$(value "$work/rel-win31-nosf/n" audioHash)" ]; then
	pass "releases (win31): with the soundFont setting off the MIDI music is silent, and nothing else changes"
else
	fail "releases (win31): the SoundFont changed more than the sound, or nothing"
fi

echo "== disks"
for c in img adf st msa stx zip-adf; do
	d="$work/disks-$c"
	python3 "$here/tests/make-synthetic.py" --container $c "$d"
	digest_run native "$d" "$frames" "$movie" > "$d/n"
	digest_run wbx "$d" "$frames" "$movie" > "$d/w"
	same "disks ($c)" "$d/n" "$work/n.txt" "$(ls "$d" | grep -v -e '^settings$' -e '^[nw]\(\.d\)\?$' | tr '\n' ' ')($(sed 's/.*"release": "\([^"]*\)".*/\1/' "$d/settings")) = the zipped folder"
	same "disks ($c)" "$d/w" "$d/n" "native = sandbox"
done
digest_run wbx "$work/disks-adf" "$frames" "$movie" --session-at "$at" > "$work/disks-adf/s"
same "disks (adf)" "$work/disks-adf/s" "$work/disks-adf/w" "session at step $at = straight"

echo "== loose"
ld="$work/loose"
python3 "$here/tests/make-synthetic.py" --release 15th --container loose "$ld"
m="$here/tests/synthetic-nth.movie"
digest_run native "$ld" 90 "$m" > "$ld/n"
digest_run native "$ld/zipped" 90 "$m" > "$ld/z"
digest_run wbx "$ld" 90 "$m" > "$ld/w"
digest_run wbx "$ld" 90 "$m" --session-at 54 > "$ld/s"
same "loose (15th)" "$ld/n" "$ld/z" "$(ls "$ld" | grep -v -e '^settings$' -e '^zipped$' -e '^[nwzs]\(\.d\)\?$' | tr '\n' ' ')(the 15th's firmware) = zipped in their folders"
same "loose (15th)" "$ld/w" "$ld/n" "native = sandbox"
same "loose (15th)" "$ld/s" "$ld/w" "session at step 54 = straight"
if [ "$(value "$ld/n" audioHash)" != "$(value "$work/rel-15th/n" audioHash)" ] && [ "$(value "$ld/n" videoHash)" = "$(value "$work/rel-15th/n" videoHash)" ]; then
	pass "loose (15th): the intro's music is the Ogg (patch 0003), the pictures the WAV release's"
else
	fail "loose (15th): the Ogg music did not play, or more than the sound changed"
fi

echo "== firmware"
# the declaration and the loader name the same files: waterbox.config's
# firmware entries a release requires = files.c's k_release_files, and its
# machines = files.c's k_releases
if python3 - "$here" <<'PY'
import json, re, sys
here = sys.argv[1]
cfg = json.load(open(here + "/waterbox.config"))
decl = set()
def releases(c):
    if "all" in c:
        r = [x for x in (releases(y) for y in c["all"]) if x]
        return r[0] if r else None
    if c.get("setting") == "release":
        return c["in"] if "in" in c else [c["is"]]
    return None
def language(c):
    for y in c.get("all", []):
        if y.get("setting") == "language": return y["is"]
    return None
for e in cfg["firmware"]:
    rs = releases(e.get("requiredWhen", {}))
    if rs and not any(y.get("setting") in ("mt32", "soundFont") for y in e["requiredWhen"].get("all", [])):
        for r in rs: decl.add((r, e["id"], language(e["requiredWhen"])))
src = open(here + "/files.c").read()
core = set((r, i, None if l == "NULL" else l.strip('"')) for r, i, l in
           re.findall(r'\{ "([^"]+)", "([^"]+)", (NULL|"[^"]+") \}', src.split("k_release_files[]")[1].split("};")[0]))
machines = [(m["when"][0], m["label"]) for m in cfg["machines"]]
labels = re.findall(r'\{ "([^"]+)", "([^"]+)" \}', src.split("k_releases[]")[1].split("};")[0])
opts = [s for s in cfg["settings"] if s["name"] == cfg["machineSetting"]][0]["options"]
ok = decl == core and machines == labels and [m[0] for m in machines] == opts
if not ok:
    print("declaration only:", sorted(decl - core, key=str), "core only:", sorted(core - decl, key=str))
    print("machines", machines, "core", labels, "options", opts)
sys.exit(0 if ok else 1)
PY
then
	pass "firmware: waterbox.config's machines and each release's firmware = the loader's tables"
else
	fail "firmware: waterbox.config and files.c disagree"
fi
# the synthetic DOS game's zip as the DOS release's firmware
mkdir -p "$work/fw-dos"
cp "$work/synth/game.zip" "$work/fw-dos/dos-disk"
printf '{"release": "dos"}' > "$work/fw-dos/settings"
digest_run native "$work/fw-dos" "$frames" "$movie" > "$work/fw-dos/n"
digest_run wbx "$work/fw-dos" "$frames" "$movie" > "$work/fw-dos/w"
same firmware "$work/fw-dos/n" "$work/n.txt" "the zip as dos-disk (release dos) = game.zip"
same firmware "$work/fw-dos/w" "$work/fw-dos/n" "native = sandbox"
# a setting counts only for the releases it is shown for: the mt32 setting on
# the Amiga's machine asks for no ROM and changes nothing
mkdir -p "$work/fw-mt32"
cp "$work/synth/game.zip" "$work/fw-mt32/"
printf '{"release": "amiga", "mt32": true}' > "$work/fw-mt32/settings"
digest_run native "$work/fw-mt32" "$frames" "$movie" > "$work/fw-mt32/n" || true
digests "$work/fw-mt32/n" > "$work/fw-mt32/n.d"
digests "$work/n.txt" > "$work/fw-mt32/ref.d"
if cmp -s "$work/fw-mt32/n.d" "$work/fw-mt32/ref.d"; then
	pass "firmware: the mt32 setting left on for the Amiga's machine asks for no ROM and changes nothing"
else
	fail "firmware: the mt32 setting on the Amiga's machine: $(sed -n 's/^loadError=//p' "$work/fw-mt32/n")"
fi
mkdir -p "$work/fw-half" "$work/fw-bad" "$work/fw-nsis"
cp "$work/disks-adf/amiga-en-disk1" "$work/fw-half/"
printf '{"release": "amiga"}' > "$work/fw-half/settings"
cp "$work/synth/game.zip" "$work/fw-bad/"
printf '{"release": "psx"}' > "$work/fw-bad/settings"
for f in Intro2004.ogg End2004.ogg lang_English.Txt settings; do cp "$work/loose/$f" "$work/fw-nsis/"; done
python3 -c "
import sys; b = bytearray(b'MZ' + b'\0' * 70000); b[0xDE00:0xDE00 + 12] = b'NullsoftInst'; open(sys.argv[1], 'wb').write(b)" "$work/fw-nsis/Pak01.pak"

echo "== settings"
mkgame seed
printf '{"randomSeed": 200, "language": "fr"}' > "$work/seed/settings"
for build in native wbx; do
	if [ $build = native ]; then run="$native"; else run="$wbx $core"; fi
	$run "$work/synth" --frames 2 --trace "$work/seed0.$build" --trace-props "Engine.Random Seed,Var[84]" > /dev/null 2>&1
	$run "$work/seed" --frames 2 --trace "$work/seed200.$build" --trace-props "Engine.Random Seed,Var[84]" > /dev/null 2>&1
	s0="$(last "$work/seed0.$build" 1) $(last "$work/seed0.$build" 2)"
	s1="$(last "$work/seed200.$build" 1) $(last "$work/seed200.$build" 2)"
	# 0x54: 0x81 for the "Out of this World" title, 1 for "Another World" (French)
	if [ "$s0" = "0 129" ] && [ "$s1" = "200 1" ]; then
		pass "settings ($build): defaults give seed 0 and the English title (0x81), randomSeed 200 + fr give 200 and the French (1)"
	else
		fail "settings ($build): seed/title $s0 (want 0 129), $s1 (want 200 1)"
	fi
	# the 20th Anniversary Edition's difficulty (0xBF) and remastered sound
	# (0xDE), as rawgl's restartAt hands them to the script
	for opt in easy:0:1 normal:1:1 hard:2:0; do
		diff_name=${opt%%:*}; rest=${opt#*:}; want_d=${rest%%:*}; want_r=${rest#*:}
		sd="$work/rel20-$diff_name"
		mkdir -p "$sd"
		cp "$work/rel-20th/20th-game" "$sd/"
		remaster=true; [ $want_r = 0 ] && remaster=false
		printf '{"release": "20th", "difficulty": "%s", "remasteredAudio": %s}' $diff_name $remaster > "$sd/settings"
		$run "$sd" --frames 2 --trace "$sd/t.$build" --trace-props "Var[191],Var[222]" > /dev/null 2>&1
		got="$(last "$sd/t.$build" 1) $(last "$sd/t.$build" 2)"
		if [ "$got" = "$want_d $want_r" ]; then
			pass "settings ($build): 20th, difficulty $diff_name remasteredAudio $remaster - the script's 0xBF $want_d, 0xDE $want_r"
		else
			fail "settings ($build): 20th, difficulty $diff_name remasteredAudio $remaster - got $got, want $want_d $want_r"
		fi
	done
done
# remasteredAudio picks the anniversary editions' sound files: the 15th's
# rmsnd/ and Music/AW/RmSnd/, the 20th's game/WGZ/original and game/OGG/original
for rel in 15th 20th; do
	sd="$work/rel-$rel-orig"
	mkdir -p "$sd"
	for f in game.zip 20th-game; do if [ -f "$work/rel-$rel/$f" ]; then cp "$work/rel-$rel/$f" "$sd/"; fi; done
	printf '{"release": "%s", "remasteredAudio": false}' $rel > "$sd/settings"
	digest_run native "$sd" 90 "$here/tests/synthetic-nth.movie" > "$sd/n"
	if [ "$(value "$sd/n" audioHash)" != "$(value "$work/rel-$rel/n" audioHash)" ] && [ "$(value "$sd/n" videoHash)" = "$(value "$work/rel-$rel/n" videoHash)" ]; then
		pass "settings: $rel, remasteredAudio false plays the original sounds (another sound, the same pictures)"
	else
		fail "settings: $rel, remasteredAudio false"
	fi
done

echo "== mt32"
mkdir -p "$work/mt32-none" "$work/mt32-bad"
for d in mt32-none mt32-bad; do
	cp "$work/synth/game.zip" "$work/$d/"
	printf '{"mt32": true}' > "$work/$d/settings"
done
head -c 65536 /dev/zero > "$work/mt32-bad/CM32L_CONTROL.ROM"
head -c 1048576 /dev/zero > "$work/mt32-bad/CM32L_PCM.ROM"
if [ -n "$roms" ]; then
	ctrl=""; pcm=""
	for f in CM32L_CONTROL.ROM cm32l_ctrl_1_02.rom; do [ -z "$ctrl" ] && [ -f "$roms/$f" ] && ctrl="$roms/$f"; done
	for f in CM32L_PCM.ROM cm32l_pcm.rom; do [ -z "$pcm" ] && [ -f "$roms/$f" ] && pcm="$roms/$f"; done
	if [ -n "$ctrl" ] && [ -n "$pcm" ]; then
		d="$work/mt32"
		mkdir -p "$d"
		cp "$work/synth/game.zip" "$d/"
		cp "$ctrl" "$d/CM32L_CONTROL.ROM"
		cp "$pcm" "$d/CM32L_PCM.ROM"
		printf '{"mt32": true}' > "$d/settings"
		digest_run native "$d" "$frames" "$movie" > "$d/n"
		digest_run wbx "$d" "$frames" "$movie" > "$d/w"
		digest_run wbx "$d" "$frames" "$movie" --rerecord > "$d/r"
		digest_run wbx "$d" "$frames" "$movie" --session-at "$at" > "$d/s"
		same mt32 "$d/w" "$d/n" "native = sandbox with the CM-32L ($(basename "$ctrl"), $(basename "$pcm"))"
		same mt32 "$d/r" "$d/w" "rerecord = straight"
		same mt32 "$d/s" "$d/w" "session at step $at = straight"
		grep -v '^audioHash=' "$d/n.d" > "$d/a"
		grep -v '^audioHash=' "$work/n.txt.d" > "$d/b"
		if cmp -s "$d/a" "$d/b" && [ "$(value "$d/n" audioHash)" != "$(value "$work/n.txt" audioHash)" ]; then
			pass "mt32: the CM-32L plays the effects instead of the samples, and nothing else changes"
		else
			fail "mt32: the MT-32 changed more than the sound, or nothing"
		fi
	else
		fail "mt32: no CM-32L ROMs in $roms"
	fi
else
	echo "(the MT-32 legs with ROMs need -r <ROM dir>)"
fi

echo "== slots"
mkdir -p "$work/slots"
cp "$work/synth/game.zip" "$work/slots/Another World (DOS).zip"
printf '{"game": ["Another World (DOS).zip"]}' > "$work/slots/slots"
"$native" "$work/slots" --frames "$frames" --movie "$movie" > "$work/slots.txt"
same slots "$work/slots.txt" "$work/n.txt" "the zip named by the slot map = game.zip"

echo "== refusals"
refuse() { # refuse <dir> <expected text> <what>
	if out="$("$native" "$work/$1" --frames 1 2>/dev/null)"; then
		fail "refusals: $3 - Init succeeded"
	elif echo "$out" | grep -q -- "$2"; then
		pass "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	else
		fail "refusals: $3 - $(echo "$out" | sed -n 's/^loadError=//p')"
	fi
}
mkdir -p "$work/empty" "$work/notzip" "$work/nodata" "$work/unknown" "$work/badpak" "$work/badseed" \
	"$work/nsis" "$work/inno" "$work/pciso" "$work/pcchd" "$work/sf-none" "$work/sf-bad"
for sd in sf-none sf-bad; do
	cp "$work/rel-win31/game.zip" "$work/$sd/"
	printf '{"release": "win31", "soundFont": true}' > "$work/$sd/settings"
done
echo "not a SoundFont" > "$work/sf-bad/soundfont.sf2"
refuse empty "Another World (DOS) needs its files (firmware): dos-disk" "no files"
echo "not a zip" > "$work/notzip/game.zip"
refuse notzip "is not a zip, a disk image" "not a zip, a disk image or a disc"
python3 - "$work" <<'PY'
import sys, zipfile
w = sys.argv[1]
with zipfile.ZipFile(w + "/nodata/game.zip", "w") as z:
    z.writestr("readme.txt", "nothing")
with zipfile.ZipFile(w + "/unknown/game.zip", "w") as z:
    z.writestr("aw/BANK01", b"\0" * 1000)        # no release rawgl knows is 1000 bytes
with zipfile.ZipFile(w + "/badpak/game.zip", "w") as z:
    z.writestr("AW15/Data/Pak01.pak", b"\0" * 16)   # not a PACK
# installers: an executable's start with NSIS's first header, or Inno Setup's
# loader data, somewhere in its first 256 KiB
nsis = bytearray(b"MZ" + b"\0" * 70000); nsis[0xDE00:0xDE00 + 12] = b"NullsoftInst"
open(w + "/nsis/AnotherWorld_full.exe", "wb").write(nsis)
open(w + "/nsis/slots", "w").write('{"game": ["AnotherWorld_full.exe"]}')
inno = bytearray(b"MZ" + b"\0" * 90000); inno[0x11800:0x11815] = b"Inno Setup Setup Data"
with zipfile.ZipFile(w + "/inno/game.zip", "w") as z:
    z.writestr("setup_another_world.exe", bytes(inno))
# a PC CD: ISO 9660's volume descriptor at sector 16, no Opera file system
iso = bytearray(2048 * 40); iso[16 * 2048:16 * 2048 + 6] = b"\x01CD001"
open(w + "/pciso/game.iso", "wb").write(iso)
PY
python3 - "$here/tests/make-synthetic.py" "$work" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("ms", sys.argv[1])
ms = importlib.util.module_from_spec(spec); spec.loader.exec_module(ms)
iso = bytearray(2048 * 40); iso[16 * 2048:16 * 2048 + 6] = b"\x01CD001"
open(sys.argv[2] + "/pcchd/game.chd", "wb").write(ms.chd_of_image(bytes(iso)))
PY
refuse nodata "hold no Another World data" "no game in the zip"
refuse unknown "No data files found" "a BANK01 of no known release (rawgl's own error, at Init)"
refuse badpak "No data files found" "a 15th Anniversary Edition's Pak01.pak that is not one (rawgl's own error, at Init)"
refuse nsis "is an NSIS installer, which the core does not open: add the game's own files" "an installer (NSIS), loose"
refuse inno "is an Inno Setup installer, which the core does not open: add the game's own files" "an installer (Inno Setup) in a zip"
refuse pciso "is a PC CD's image, not the 3DO's disc: .*add the 15th" "a PC CD's image (.iso)"
refuse pcchd "is a PC CD, not the 3DO's disc: .*add the 15th" "a PC CD (.chd)"
cp "$work/synth/game.zip" "$work/badseed/game.zip"
printf '{"randomSeed": 70000}' > "$work/badseed/settings"
refuse badseed "goes from 0 to 65535" "randomSeed 70000"
refuse mt32-none "CM32L_CONTROL.ROM is not there" "mt32 without the CM-32L's ROMs"
refuse mt32-bad "is not a Roland ROM Munt knows" "mt32 with ROMs that are not"
refuse sf-none "soundfont.sf2 is not there" "soundFont without the SoundFont"
refuse sf-bad "soundfont.sf2 is not a SoundFont" "soundFont with a file that is not one"
refuse fw-half "needs its file amiga-en-disk2 (firmware), which is not there" "the Amiga's machine with one of its two disks"
refuse fw-bad "none of Another World's releases" "a release the core does not have"
refuse fw-nsis "Pak01.pak is an NSIS installer, which the core does not open" "the 15th's Pak01.pak that is its installer"

echo "== halts"
for kind in opcode shape; do
	mkgame "bad$kind" "--bad-$kind"
	"$native" "$work/bad$kind" --frames 60 --movie "$movie" --trace "$work/bad$kind.t" --trace-props "Machine.Halted,Machine.Steps" > "$work/bad$kind.n" 2> "$work/bad$kind.err"
	"$wbx" "$core" "$work/bad$kind" --frames 60 --movie "$movie" > "$work/bad$kind.w" 2>/dev/null
	why="$(grep -m1 ERROR "$work/bad$kind.err" || true)"
	if [ "$(last "$work/bad$kind.t" 1)" = 1 ] && [ "$(tail -1 "$work/bad$kind.t" | awk '{print $2}')" = 1000/20 ] && [ -n "$why" ]; then
		pass "halts ($kind): halted and still stepping at 50 Hz - $why"
	else
		fail "halts ($kind): not halted, or no reason"
	fi
	same halts "$work/bad$kind.w" "$work/bad$kind.n" "($kind) native = sandbox"
done

echo "== clock"
if nm "$core" | grep -qE ' (T|W|t) (time|__real_time)$'; then
	fail "clock: the guest links a time() of the C library's"
else
	pass "clock: time() is only the core's (the randomSeed setting); getTimeStamp() is the machine's milliseconds"
fi

echo "== teeth"
# step 42 is in 16003, the joystick's part: Left instead of Right
awk '!/^#/ { n++ } n == 43 && !/^#/ { print "L"; next } { print }' "$movie" > "$work/teeth.movie"
"$native" "$work/synth" --frames "$frames" --movie "$work/teeth.movie" > "$work/teeth.txt"
if digests "$work/teeth.txt" | cmp -s - "$work/n.txt.d"; then
	fail "teeth: a movie one step different digests the same"
else
	pass "teeth: a movie one step different digests differently"
fi

if [ -n "$chimera_run" ]; then
	echo "== engine ($chimera_run)"
	"$here/build-package.sh" -m "$mb" -o "$work/package" > "$work/package.log" 2>&1 || {
		tail -5 "$work/package.log"; fail "engine: the package did not build"; }
	# a line is |console buttons (Code, Pause, the letters, Backspace)|P1 (Up Down Left Right Action)|
	python3 - "$work/engine.txt" <<'PY'
import sys
idle = "|" + "." * 29 + "|" + "." * 5 + "|"
fire = "|" + "." * 29 + "|" + "....F" + "|"
open(sys.argv[1], "w").write("\n".join([idle] * 10 + [fire] + [idle] * 49) + "\n")
PY
	part() { python3 -c "import struct, sys; print(struct.unpack('<H', open(sys.argv[1], 'rb').read()[:2])[0])" "$1"; }
	( cd "$work" && "$chimera_run" "$work/package/rawgl.chimeraCore" "$work/synth/game.zip" "$work/engine.txt" --firmware "dos-disk=$work/synth/game.zip" --dump "Game State=$work/engine.gs" ) > "$work/engine.out" 2>&1 || true
	( cd "$work" && "$chimera_run" "$work/package/rawgl.chimeraCore" "$work/synth/game.zip" "$work/engine.txt" --rerecord --firmware "dos-disk=$work/synth/game.zip" --dump "Game State=$work/engine-r.gs" ) > "$work/engine-r.out" 2>&1 || true
	if [ -f "$work/engine.gs" ] && [ "$(part "$work/engine.gs")" = 16003 ] && cmp -s "$work/engine.gs" "$work/engine-r.gs"; then
		pass "engine: chimera-run plays fire at step 10 to part 16003 by step 60, the same with --rerecord"
	else
		fail "engine: chimera-run"; tail -3 "$work/engine.out"
	fi
fi

if [ -n "$game" ]; then
	echo "== Another World ($(echo "$game" | tr '\n' ' ')${game_movie:+, $game_movie})"
	mkdir -p "$work/game"
	# as a project mounts them: <id>=<file> is the release's firmware under its
	# id (-R names the release); a SoundFont (.sf2) is the soundfont.sf2
	# firmware, with the soundFont setting; any other file goes in a "game"
	# slot, as a host that is no project's may bring it
	slots='{"game": ['
	sep=''
	sf=''
	while IFS= read -r f; do
		case "$f" in
			*=*)
				ln -sf "$(cd "$(dirname "${f#*=}")" && pwd)/$(basename "${f#*=}")" "$work/game/${f%%=*}" ;;
			*.sf2|*.SF2)
				cp "$f" "$work/game/soundfont.sf2"; sf=', "soundFont": true' ;;
			*)
				cp "$f" "$work/game/$(basename "$f")"
				slots="$slots$sep\"$(basename "$f")\""; sep=', ' ;;
		esac
	done <<GAMES
$game
GAMES
	slots="$slots]"
	printf '%s}' "$slots" > "$work/game/slots"
	printf '{"release": "%s"%s}' "${game_release:-dos}" "$sf" > "$work/game/settings"
	gn=2000
	gm=""
	if [ -n "$game_movie" ]; then
		gn=$(grep -vc '^#' "$game_movie")
		gm="--movie $game_movie"
	fi
	gat=$((gn * 9 / 10))
	"$native" "$work/game" --frames $gn $gm > "$work/g.n"
	"$wbx" "$core" "$work/game" --frames $gn $gm > "$work/g.w" 2>/dev/null
	same game "$work/g.w" "$work/g.n" "native = sandbox, $gn steps from power-on (clock $(value "$work/g.n" clock) ms)"
	"$wbx" "$core" "$work/game" --frames $gn $gm --rerecord > "$work/g.r" 2>/dev/null
	same game "$work/g.r" "$work/g.w" "rerecord = straight"
	"$wbx" "$core" "$work/game" --frames $gn $gm --session-at $gat > "$work/g.s" 2>/dev/null
	same game "$work/g.s" "$work/g.w" "session at $gat = straight"
fi

echo
if [ $fails -eq 0 ]; then
	echo "gate: all legs passed"
else
	echo "gate: $fails leg(s) failed"
	exit 1
fi
