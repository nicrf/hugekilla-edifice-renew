#!/usr/bin/env bash
# Runs the network tests (tests/net_*.py) against a headless local server, one
# fresh server per test. Fails if a test fails, the server dies, or
# AddressSanitizer reports anything. Used by CI on Linux; also works locally:
#   tests/run_netplay_tests.sh build/linux-debug/Edifice
# Linux needs Xvfb (a virtual display) unless DISPLAY is already set.
set -u
BIN="$1"
LOGDIR="${2:-netplay-logs}"
mkdir -p "$LOGDIR"
cd "$(dirname "$0")/.."

if [ -z "${DISPLAY:-}" ] && [ "$(uname)" = "Linux" ]; then
	Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
	XVFB=$!
	export DISPLAY=:99
	sleep 2
fi

export SDL_AUDIODRIVER=dummy
export ASAN_OPTIONS=detect_leaks=0
export HK_AUTOSTART=1 HK_SHOT=/dev/null HK_SHOT_FRAME=999999

status=0
for t in net_handshake net_identity net_join net_fuzz net_voice net_map; do
	log="$LOGDIR/$t.log"
	args=()
	if [ "$t" = net_map ]; then
		# a server started with a saved map (HK_MAPFILE), written by the test itself
		python3 tests/net_map.py --write "$LOGDIR/test.map" || status=1
		args=("$LOGDIR/test.map")
		HK_MAPFILE="$LOGDIR/test.map" "$BIN" >"$log" 2>&1 &
	else
		"$BIN" >"$log" 2>&1 &
	fi
	pid=$!
	echo "== $t"
	python3 "tests/$t.py" "${args[@]+"${args[@]}"}" || status=1
	if ! kill -0 "$pid" 2>/dev/null; then
		echo "server died during $t"
		status=1
	fi
	kill "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
	if grep -q "ERROR: AddressSanitizer" "$log"; then
		echo "AddressSanitizer report during $t:"
		cat "$log"
		status=1
	fi
	sleep 1   # let the ports go
done

[ -n "${XVFB:-}" ] && kill "$XVFB" 2>/dev/null
exit $status
