#!/usr/bin/env bash
# Netplay through GameNetworkingSockets (docs/M7.md, M7b) against headless local
# servers started with HK_TRANSPORT=gns. Needs a build with -DEDIFICE_GNS=ON. Used by
# CI (gns job, Linux); also works locally:
#   tests/run_gns_netplay_test.sh build/gns/Edifice build/gns/gns_netplay_test
#  1. tests/gns_netplay_test.cpp: two clients (Sock over GnsLink) join, get the land
#     (LANDINFO), play and talk next to the host, who hears them
#  2. a second game instance joins (HK_JOIN) through GameNetworkingSockets
#  3. the UDP tests (run_netplay_tests.sh) against servers in GNS mode: plain UDP
#     clients still join them as before
# Linux needs Xvfb (a virtual display) unless DISPLAY is already set.
set -u
BIN="$1"
TEST="$2"
LOGDIR="${3:-gns-logs}"
mkdir -p "$LOGDIR"
cd "$(dirname "$0")/.."

if [ -z "${DISPLAY:-}" ] && [ "$(uname)" = "Linux" ]; then
	Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
	XVFB=$!
	export DISPLAY=:99
	sleep 2
fi

export SDL_AUDIODRIVER=dummy HK_SHOT=/dev/null HK_SHOT_FRAME=999999
export HK_TRANSPORT=gns HK_CONSOLE_LOG=1
status=0

# waits up to $3 seconds for the text $2 in the file $1
waitfor() {
	for _ in $(seq "$3"); do
		grep -q "$2" "$1" 2>/dev/null && return 0
		sleep 1
	done
	return 1
}

# stops the process $1; fails if it died before
stop() {
	if ! kill -0 "$1" 2>/dev/null; then
		echo "FAIL: $2 died"
		status=1
	fi
	kill "$1" 2>/dev/null
	wait "$1" 2>/dev/null
}

echo "== gns_netplay_test"
server="$LOGDIR/server.log"
HK_AUTOSTART=1 HK_VOICE_TEST_TONE=1 HK_VOICE_AUTOTALK=1 HK_VOICE_LOG=1 "$BIN" >"$server" 2>&1 &
pid=$!
if waitfor "$server" "gns: listening" 60; then
	"$TEST" || status=1
	if waitfor "$server" "voice: hearing player [1-9]" 5; then echo "ok   the host hears a client"
	else echo "FAIL the host hears no client"; status=1; fi
else
	echo "FAIL: the server doesn't listen through GameNetworkingSockets"
	status=1
fi
stop $pid server
sleep 1

echo "== two game instances"
server="$LOGDIR/server2.log"
client="$LOGDIR/client.log"
HK_AUTOSTART=1 "$BIN" >"$server" 2>&1 &
pid=$!
waitfor "$server" "gns: listening" 60
HK_JOIN=127.0.0.1 "$BIN" >"$client" 2>&1 &
cpid=$!
if waitfor "$client" "console: entering game" 60; then echo "ok   the client entered the game"
else echo "FAIL the client didn't enter the game"; status=1; fi
if grep -q "gns: connected with .*, encrypted" "$client"; then echo "ok   through an encrypted connection"
else echo "FAIL no encrypted connection"; status=1; fi
if grep -q "console: received landinfo" "$client"; then echo "ok   the client got the land"
else echo "FAIL no LANDINFO"; status=1; fi
stop $cpid client
stop $pid server
sleep 1

echo "== UDP clients, servers in GNS mode"
tests/run_netplay_tests.sh "$BIN" "$LOGDIR/udp" || status=1

if [ $status != 0 ]; then
	for f in "$LOGDIR"/*.log; do echo "---- $f"; tail -60 "$f"; done
fi
[ -n "${XVFB:-}" ] && kill "$XVFB" 2>/dev/null
exit $status
