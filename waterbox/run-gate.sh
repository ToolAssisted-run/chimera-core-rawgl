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
#                Windows 3.1 and the 3DO (its folder, and its disc image read
#                in place): native = sandbox, rerecord, session, each; the
#                3DO's folder and disc the same machine; Jump the 3DO's only; the SoundFont changes Windows 3.1's sound and
#                nothing else
#   settings     randomSeed is what the script's seed starts at, language what
#                the DOS copy protection's title choice reads, difficulty and
#                remasteredAudio what the anniversary editions read (both
#                builds)
#   slots        the project's slot map names the zip
#   mt32         the DOS release's sound effects on a CM-32L (Munt), with the
#                ROMs -r names: native = sandbox, rerecord, session, and only
#                the sound changes; without ROMs, or with a file that is not
#                one, a refusal that names it
#   refusals     no zip, not a zip, no game in it, data rawgl cannot tell, a
#                15th Anniversary Edition's Pak01.pak that is not one, a seed
#                out of range: each says why
#   halts        rawgl's error() and a failed assertion halt the machine, which
#                keeps stepping; the same in both builds
#   clock        the guest has no time() of its own: the one it calls is the
#                core's (the setting), and nothing of the engine reads a clock
#   teeth        the equivalence comparison sees a one-step difference
#   engine       (with -c) the package through Chimera's own engine, headless:
#                chimera-run plays a movie in Chimera's format (console
#                buttons, then P1's), with and without rerecording
#
# usage: run-gate.sh [-m <miniBox dir>] [-g <Another World zip or iso> [-M <movie>]] [-f <frames>]
#                    [-c <chimera-run>] [-r <ROM dir>]
#   -g adds the equivalence, rerecord and session legs on a real release (the
#      zip of the game's folder, or the 3DO's disc, as a project would bring it;
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
while getopts "m:g:f:c:r:M:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		g) game="$OPTARG" ;;
		f) frames="$OPTARG" ;;
		c) chimera_run="$OPTARG" ;;
		r) roms="$OPTARG" ;;
		M) game_movie="$OPTARG" ;;
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
for rel in 15th 20th win31 3do 3do-iso; do
	case $rel in
		15th|20th) m="$here/tests/synthetic-nth.movie"; n=90 ;;
		win31) m="$movie"; n=100 ;;
		*) m="$here/tests/synthetic-3do.movie"; n=110 ;;
	esac
	d="$work/rel-$rel"
	mkdir -p "$d"
	if [ $rel = 3do-iso ]; then python3 "$here/tests/make-synthetic.py" --release $rel "$d/game.iso"
	else python3 "$here/tests/make-synthetic.py" --release $rel "$d/game.zip"; fi
	[ $rel = win31 ] && cp "$root/extern/TinySoundFont/examples/florestan-subset.sf2" "$d/soundfont.sf2"
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
if [ "$(value "$work/rel-3do/n" activeButtons)" = 35 ] && [ "$(value "$work/rel-15th/n" activeButtons)" = 34 ]; then
	pass "releases: Jump is the 3DO's only (35 buttons there, 34 elsewhere)"
else
	fail "releases: active buttons 3DO $(value "$work/rel-3do/n" activeButtons), 15th $(value "$work/rel-15th/n" activeButtons)"
fi
mkdir -p "$work/rel-win31-nosf"
cp "$work/rel-win31/game.zip" "$work/rel-win31-nosf/"
digest_run native "$work/rel-win31-nosf" 100 "$movie" > "$work/rel-win31-nosf/n"
grep -v '^audioHash=' "$work/rel-win31/n.d" > "$work/sf.a"
digests "$work/rel-win31-nosf/n" | grep -v '^audioHash=' > "$work/sf.b"
if cmp -s "$work/sf.a" "$work/sf.b" && [ "$(value "$work/rel-win31/n" audioHash)" != "$(value "$work/rel-win31-nosf/n" audioHash)" ]; then
	pass "releases (win31): without a SoundFont the MIDI music is silent, and nothing else changes"
else
	fail "releases (win31): the SoundFont changed more than the sound, or nothing"
fi

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
		cp "$work/rel-20th/game.zip" "$sd/"
		remaster=true; [ $want_r = 0 ] && remaster=false
		printf '{"difficulty": "%s", "remasteredAudio": %s}' $diff_name $remaster > "$sd/settings"
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
	cp "$work/rel-$rel/game.zip" "$sd/"
	printf '{"remasteredAudio": false}' > "$sd/settings"
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
mkdir -p "$work/empty" "$work/notzip" "$work/nodata" "$work/unknown" "$work/badpak" "$work/badseed"
refuse empty "needs the game's files" "no zip"
echo "not a zip" > "$work/notzip/game.zip"
refuse notzip "is not a zip file" "not a zip"
python3 - "$work" <<'PY'
import sys, zipfile
w = sys.argv[1]
with zipfile.ZipFile(w + "/nodata/game.zip", "w") as z:
    z.writestr("readme.txt", "nothing")
with zipfile.ZipFile(w + "/unknown/game.zip", "w") as z:
    z.writestr("aw/BANK01", b"\0" * 1000)        # no release rawgl knows is 1000 bytes
with zipfile.ZipFile(w + "/badpak/game.zip", "w") as z:
    z.writestr("AW15/Data/Pak01.pak", b"\0" * 16)   # not a PACK
PY
refuse nodata "holds no Another World data" "no game in the zip"
refuse unknown "No data files found" "a BANK01 of no known release (rawgl's own error, at Init)"
refuse badpak "No data files found" "a 15th Anniversary Edition's Pak01.pak that is not one (rawgl's own error, at Init)"
cp "$work/synth/game.zip" "$work/badseed/game.zip"
printf '{"randomSeed": 70000}' > "$work/badseed/settings"
refuse badseed "goes from 0 to 65535" "randomSeed 70000"
refuse mt32-none "CM32L_CONTROL.ROM is not there" "mt32 without the CM-32L's ROMs"
refuse mt32-bad "is not a Roland ROM Munt knows" "mt32 with ROMs that are not"

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
	( cd "$work" && "$chimera_run" "$work/package/rawgl.chimeraCore" "$work/synth/game.zip" "$work/engine.txt" --dump "Game State=$work/engine.gs" ) > "$work/engine.out" 2>&1 || true
	( cd "$work" && "$chimera_run" "$work/package/rawgl.chimeraCore" "$work/synth/game.zip" "$work/engine.txt" --rerecord --dump "Game State=$work/engine-r.gs" ) > "$work/engine-r.out" 2>&1 || true
	if [ -f "$work/engine.gs" ] && [ "$(part "$work/engine.gs")" = 16003 ] && cmp -s "$work/engine.gs" "$work/engine-r.gs"; then
		pass "engine: chimera-run plays fire at step 10 to part 16003 by step 60, the same with --rerecord"
	else
		fail "engine: chimera-run"; tail -3 "$work/engine.out"
	fi
fi

if [ -n "$game" ]; then
	echo "== Another World ($game${game_movie:+, $game_movie})"
	mkdir -p "$work/game"
	case "$game" in
		*.iso|*.ISO) cp "$game" "$work/game/game.iso" ;;
		*) cp "$game" "$work/game/game.zip" ;;
	esac
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
