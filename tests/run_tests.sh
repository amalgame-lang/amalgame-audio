#!/bin/bash
# ─────────────────────────────────────────────────────
#  amalgame-audio — Test Runner
#  Usage: ./tests/run_tests.sh [/path/to/amc]
#
#  Self-contained: no audio device required (Audio.Play is
#  intentionally NOT in the test suite — silent CI machines have
#  no usable output). Tests cover synth, transform, WAV
#  round-trip; the samples/submarine_ping.am demo covers
#  playback for users who want to verify their setup.
# ─────────────────────────────────────────────────────

set -u

if [ $# -ge 1 ]; then
    AMC="$1"
elif [ -n "${AMC:-}" ]; then
    :
elif command -v amc >/dev/null 2>&1; then
    AMC="$(command -v amc)"
else
    echo "ERROR: amc not found." >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PKG_RUNTIME="$PKG_ROOT/runtime"
PKG_VENDOR="$PKG_ROOT/runtime/vendor"

AMC_DIR="$(cd "$(dirname "$AMC")" && pwd)"
if [ -d "$AMC_DIR/runtime" ]; then
    AMC_RUNTIME="$AMC_DIR/runtime"
elif [ -n "${AMC_RUNTIME:-}" ]; then
    :
else
    echo "ERROR: amc runtime/ not found. Set AMC_RUNTIME=..." >&2
    exit 2
fi

BUILD_DIR="$(mktemp -d -t aaudio-XXXXXX)"
trap 'rm -rf "$BUILD_DIR"' EXIT
PROJ_DIR="$BUILD_DIR/proj"
mkdir -p "$PROJ_DIR"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'
PASS=0; FAIL=0; SKIP=0

echo ""
echo "════════════════════════════════════════════"
echo "  amalgame-audio — Tests"
echo "════════════════════════════════════════════"
echo "  amc:     $AMC ($("$AMC" --version 2>&1 | head -1))"
echo "  runtime: $AMC_RUNTIME"
echo ""

# ── Precompile ma_impl.c once (slow — ~15-20s) ──────
echo "── Precompiling vendored ma_impl.c (~15-20s) ──"
MA_IMPL_O="$BUILD_DIR/ma_impl.o"
if ! gcc -O2 -w -c \
        -I"$AMC_RUNTIME" \
        "$PKG_VENDOR/ma_impl.c" \
        -o "$MA_IMPL_O" 2>"$BUILD_DIR/ma.log"; then
    echo -e "${RED}FAIL${NC} ma_impl.c compile:"
    cat "$BUILD_DIR/ma.log" | head -5 | sed 's/^/    /'
    exit 1
fi
echo "  ma_impl.o: $(stat -c%s "$MA_IMPL_O" 2>/dev/null || stat -f%z "$MA_IMPL_O") bytes"
echo ""

# ── Stage a fake cache pointing at the working tree ──
FAKE_CACHE="$BUILD_DIR/cache"
PKG_GIT="github.com/amalgame-lang/amalgame-audio"
PKG_TAG="${PKG_TAG:-v0.4.0}"
FAKE_SHA="deadbeefcafebabe0000000000000000000000ab"
SHORT_SHA="${FAKE_SHA:0:8}"
PKG_CACHE_DIR="$FAKE_CACHE/$PKG_GIT/${PKG_TAG}_${SHORT_SHA}"

mkdir -p "$(dirname "$PKG_CACHE_DIR")"
rm -rf "$PKG_CACHE_DIR"
ln -s "$PKG_ROOT" "$PKG_CACHE_DIR"

cat > "$PROJ_DIR/amalgame.lock" <<EOF
[[package]]
name = "amalgame-audio"
git  = "$PKG_GIT"
tag  = "$PKG_TAG"
rev  = "$FAKE_SHA"
EOF

export AMALGAME_PACKAGES_DIR="$FAKE_CACHE"
echo "  cache:   $FAKE_CACHE → $PKG_ROOT"
echo ""

run_test() {
    local name="$1"
    local expected="$2"
    local skip_marker="${3:-}"
    printf "  %-38s" "$name"
    cp "$SCRIPT_DIR/stdlib_audio.am" "$PROJ_DIR/test.am"
    local out_base="$PROJ_DIR/test"
    local out
    out=$(cd "$PROJ_DIR" && "$AMC" -o test test.am 2>&1)
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAIL${NC} (amc)"; echo "$out" | head -3 | sed 's/^/    /'
        FAIL=$((FAIL + 1)); return
    fi
    if [ ! -f "$out_base.c" ]; then
        echo -e "${RED}FAIL${NC} (no .c)"; FAIL=$((FAIL + 1)); return
    fi
    # Two-TU link: user .c + precompiled ma_impl.o + miniaudio's link deps
    gcc -O2 -w \
        -I"$AMC_RUNTIME" -I"$PKG_RUNTIME" -I"$PKG_VENDOR" \
        "$out_base.c" "$MA_IMPL_O" \
        -lgc -lm -lcurl -ldl -lpthread \
        -o "$out_base" 2>"$BUILD_DIR/link.log"
    if [ ! -x "$out_base" ]; then
        echo -e "${RED}FAIL${NC} (gcc link)"
        cat "$BUILD_DIR/link.log" | head -3 | sed 's/^/    /'
        FAIL=$((FAIL + 1)); return
    fi
    local run_output
    run_output=$("$out_base" 2>&1)
    if echo "$run_output" | grep -qF "$expected"; then
        echo -e "${GREEN}PASS${NC}"; PASS=$((PASS + 1))
    elif [ -n "$skip_marker" ] && echo "$run_output" | grep -qF "$skip_marker"; then
        echo -e "${YELLOW}SKIP${NC} (no capture device)"
        SKIP=$((SKIP + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo "    expected: $expected"
        echo "    got:      $(echo "$run_output" | head -3 | tr '\n' '|')"
        FAIL=$((FAIL + 1))
    fi
}

echo "── Audio ───────────────────────────────────"
run_test "GenSine length"        "[PASS] GenSine length"
run_test "GenSilence is silent"  "[PASS] GenSilence is silent"
run_test "sine peak ~int16 max"  "[PASS] sine peak ~int16 max"
run_test "envelope ramps up"     "[PASS] envelope first sample = 0"
run_test "Scale 0.5"             "[PASS] Scale 0.5 halves sample 1000"
run_test "Mix identity"          "[PASS] Mix with silence is identity"
run_test "Echo length"           "[PASS] Echo length matches formula"
run_test "SaveAsWav"             "[PASS] SaveAsWav"
run_test "LoadWav length"        "[PASS] LoadWav length round-trip"
run_test "WAV round-trip exact"  "[PASS] WAV round-trip sample-exact"

echo ""
echo "── Audio v0.2 ──────────────────────────────"
run_test "SampleRateOf 44100"    "[PASS] SampleRateOf returns 44100"
run_test "ChannelCountOf mono"   "[PASS] ChannelCountOf returns 1 for mono WAV"
run_test "DurationMsOf 100ms"    "[PASS] DurationMsOf returns 100 for 0.1s sine"
run_test "Load auto-detect"      "[PASS] Load auto-detects WAV"
run_test "Probe missing file"    "[PASS] SampleRateOf rejects missing file"

echo ""
echo "── Audio v0.3 (capture) ────────────────────"
# These tests SKIP cleanly when no default capture device is
# available (typical CI). On a developer box with a working mic
# they exercise both the blocking and non-blocking surfaces.
run_test "Record blocking length"    "[PASS] Record blocking length"            "[SKIP] Record blocking"
run_test "RecordStart/Stop trio"     "[PASS] RecordStart/Stop handle roundtrip" "[SKIP] RecordStart/Stop"

echo ""
echo "── Audio v0.4 (streaming playback) ─────────"
# Same SKIP gating as capture — silent CI machines have no
# default output device, so PlayStart returns null and these
# tests SKIP cleanly. On a developer box they exercise the full
# state machine (Start → IsActive → Pause → IsPaused → Resume →
# IsPaused → Stop).
run_test "PlayStart active handle"   "[PASS] PlayStart returns active handle"   "[SKIP] PlayStart"
run_test "PlayPause/Resume toggle"   "[PASS] PlayPause/Resume toggles flag"     "[SKIP] PlayPause/Resume"
run_test "PlayStop returns true"     "[PASS] PlayStop returns true on live handle" "[SKIP] PlayStop"

echo ""
echo "────────────────────────────────────────────"
echo -e "  ${GREEN}PASS: $PASS${NC}  |  ${RED}FAIL: $FAIL${NC}  |  ${YELLOW}SKIP: $SKIP${NC}"
echo "────────────────────────────────────────────"
echo ""

[ $FAIL -eq 0 ] && exit 0 || exit 1
