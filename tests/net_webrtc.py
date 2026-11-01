#!/usr/bin/env python3
"""A browser joins a native host over WebRTC (roadmap M14 task 3), end to end on
one machine:
    python3 tests/net_webrtc.py <Edifice binary or .app> <edifice-lobby binary> [build/web]
                                [--relay] [--headed] [--logs DIR]
Needs a native build with GameNetworkingSockets and the WebRTC bridge (python3
build.py, i.e. --gns, EDIFICE_WEBRTC), the web build (python3 build.py --web) and
Playwright (pip install playwright; playwright install chromium-headless-shell).

  1. a local edifice-lobby (server/): plain HTTP on 127.0.0.1 with its WebSocket
     signaling relay, CORS for the page's origin, -allow-private, STUN/TURN on this
     machine's LAN address (neither libjuice nor Chromium use loopback for ICE);
  2. a native host (HK_AUTOSTART) with its own user folder (HK_USERDIR, a CRLF
     net.ini: lobby= the local one, servervisibility=public, ports 37220/37221),
     listed with "webrtc": true;
  3. the web build in headless Chromium, its net.ini pointing to the same lobby,
     joins the game by its code (HK_LOBBYJOIN, the Internet tab) and spawns
     (HK_PLAY): the lobby session, the offer, answer and candidates over the relay,
     then the DataChannel carries the game protocol (INFO, REQUESTENTER without a
     join cookie, the land, the game);
  4. a shot (mouse button) and a block edit (Module._HKWebSendBlock) must reach the
     host (HK_NET_EVENTS logs them as they come in), and the lobby must list 2
     players.
--relay: p2p=relay on both sides (TURN relay candidates only: the DataChannel goes
through the lobby's TURN server). Nothing is contacted but this machine (the
public lobby never: every net.ini here names the local one).
NAT: everything runs on one machine, so there is no NAT between the two players;
what is tested is the signaling, ICE, DTLS/SCTP, TURN and the game over them.
On Linux the native game needs a display (xvfb-run). Local test, not in the CI:
it needs the native GameNetworkingSockets/libdatachannel build and the web build in
the same job, and the netplay tests run locally (see BUILD.md)."""
import argparse
import functools
import http.server
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOSTPORT = 37220     # not 27020: a real game may run on this machine
failures = 0
procs = []


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1
    return cond


def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def lan_address():
    if os.environ.get("HK_P2P_ADDRESS"):
        return os.environ["HK_P2P_ADDRESS"]
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))   # TEST-NET: nothing is sent, the route picks the address
        return s.getsockname()[0]
    finally:
        s.close()


# the page whose events Playwright delivers while we wait (its sync API only does
# so inside its own calls, never during time.sleep)
PAGE = []


def wait_for(what, cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        r = cond()
        if r:
            return r
        if PAGE:
            PAGE[0].wait_for_timeout(250)
        else:
            time.sleep(0.25)
    check(False, what + " (timed out after %d s)" % timeout)
    return None


def read(path):
    try:
        with open(path, errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def serve(site):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                              **{".wasm": "application/wasm", ".data": "application/octet-stream"})

        def log_message(self, *a):
            pass
    port = free_port()
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Quiet, directory=site))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, "http://127.0.0.1:%d" % port


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("lobby")
    ap.add_argument("site", nargs="?", default=os.path.join(ROOT, "build", "web"))
    ap.add_argument("--relay", action="store_true", help="TURN relay candidates only, both sides")
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--logs", default=os.path.join(ROOT, "build", "webrtc-logs"))
    args = ap.parse_args()
    game = os.path.abspath(args.game)
    if game.endswith(".app"):
        game = os.path.join(game, "Contents", "MacOS", "Edifice")
    if not os.path.isfile(os.path.join(args.site, "index.wasm")):
        sys.exit("no web build in %s (python3 build.py --web)" % args.site)
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        sys.exit("needs Playwright: pip install playwright && playwright install chromium-headless-shell")
    os.makedirs(args.logs, exist_ok=True)
    work = tempfile.mkdtemp(prefix="edifice-webrtc-")
    address = lan_address()
    check(not address.startswith("127."), "LAN address for ICE and TURN: " + address)
    httpd, origin = serve(args.site)
    lobbyport, turnport = free_port(), free_port(socket.SOCK_DGRAM)
    lobbyurl = "http://127.0.0.1:%d" % lobbyport
    lobbylog = os.path.join(args.logs, "lobby.log")
    hostlog = os.path.join(args.logs, "host.log")
    pagelog = os.path.join(args.logs, "page.log")
    mode = "relay" if args.relay else "direct"
    try:
        procs.append(subprocess.Popen(
            [os.path.abspath(args.lobby), "-http", "127.0.0.1:%d" % lobbyport, "-signal", "off",
             "-turn", ":%d" % turnport, "-public-host", address, "-turn-relay-ip", address,
             "-allow-private", "-cors-origins", origin, "-rate-limit", "600"],
            stdout=open(lobbylog, "w"), stderr=subprocess.STDOUT))

        def healthy():
            try:
                return urllib.request.urlopen(lobbyurl + "/healthz", timeout=1).status == 200
            except OSError:
                return False
        if not wait_for("the local lobby answers", healthy, 20):
            return

        # the native host, with its own user folder (never the player's)
        hostdir = os.path.join(work, "host") + os.sep
        os.makedirs(hostdir)
        with open(os.path.join(hostdir, "net.ini"), "w", newline="\r\n") as f:
            f.write("infosendport=%d\ndatasendport=%d\nservername=WebRTC test host\nname=host\n"
                    "lobby=%s\nservervisibility=public\n" % (HOSTPORT, HOSTPORT + 1, lobbyurl))
        env = dict(os.environ, HK_USERDIR=hostdir, HK_AUTOSTART="1", HK_CONSOLE_LOG="1", HK_NET_EVENTS="1",
                   SDL_AUDIODRIVER="dummy", ASAN_OPTIONS="detect_leaks=0")
        if args.relay:
            env["HK_P2P"] = "relay"
        procs.append(subprocess.Popen([game], stdout=open(hostlog, "w"), stderr=subprocess.STDOUT, env=env))

        def listed():
            try:
                with urllib.request.urlopen(lobbyurl + "/v1/servers?protocol=0", timeout=2) as r:
                    for s in json.loads(r.read())["servers"]:
                        if s["name"] == "WebRTC test host" and s.get("identity"):
                            return s
            except (OSError, ValueError, KeyError):
                pass
            return None
        listing = wait_for("the host is listed with its identity", listed, 60)
        if not listing:
            return
        check(listing.get("webrtc") is True, "the host is listed as taking browsers (webrtc)")

        with sync_playwright() as p:
            browser = p.chromium.launch(headless=not args.headed,
                                        args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader",
                                              "--ignore-gpu-blocklist", "--autoplay-policy=no-user-gesture-required"])
            page = browser.new_context(viewport={"width": 1024, "height": 700}).new_page()
            PAGE.append(page)
            log, errors = [], []
            page.on("console", lambda m: log.append(m.text))
            page.on("pageerror", lambda e: errors.append(str(e)))

            def load(query=""):
                page.goto(origin + "/" + query)
                page.wait_for_function("document.getElementById('status').classList.contains('hidden')",
                                       timeout=60000)
            load()
            # the page's net.ini: the local lobby (never the public one), and p2p
            page.evaluate("""([lobby, relay]) => new Promise(r => {
                const f = '/user/net.ini';
                let ini = FS.readFile(f, {encoding: 'utf8'}).replace(/\\r?\\n?^(lobby|name|p2p)=.*$/mg, '');
                ini += '\\r\\nlobby=' + lobby + '\\r\\nname=WebPlayer\\r\\n' + (relay ? 'p2p=relay\\r\\n' : '');
                FS.writeFile(f, ini);
                FS.syncfs(false, r);
            })""", [lobbyurl, args.relay])
            load("?HK_LOBBYJOIN=%s&HK_PLAY=1&HK_CONSOLE_LOG=1" % listing["id"])

            def logged(text):
                return lambda: any(text in l for l in log)
            entered = wait_for("the browser enters the game (%s)" % mode, logged("entering game"), 60)
            check(any("webrtc: DataChannel open with " + listing["identity"] in l for l in log),
                  "a DataChannel to the listed identity (%s)" % mode)
            check(any("signaling: connected to ws://127.0.0.1:%d/v1/signal" % lobbyport in l for l in log),
                  "the page signals over the lobby's WebSocket relay")
            if entered:
                check(wait_for("the land arrives", logged("received landinfo"), 30) is not None,
                      "the browser got the land (%s)" % mode)
                # HK_PLAY spawned it, or a click does (a shot on the spawn screen, or once
                # the respawn delay is over after a death); then shots
                page.mouse.move(512, 350)
                states = []
                end = time.time() + 30
                while time.time() < end and states[-6:].count(10) < 6:
                    page.mouse.down()
                    page.wait_for_timeout(300)
                    page.mouse.up()
                    page.wait_for_timeout(300)
                    states.append(page.evaluate("Module._HKWebPlayState()"))
                print("     play states while clicking: %s (10 playing, 11 spawn screen or dead)" % states)
                check(10 in states, "the browser player spawned and played (%s)" % mode)
                page.evaluate("Module._HKWebSendBlock()")
                check(wait_for("a shot reaches the host", lambda: "net: shot from player 1" in read(hostlog), 15)
                      is not None, "a shot from the browser reaches the host (%s)" % mode)
                check(wait_for("a block edit reaches the host",
                               lambda: "net: blocks from player 1" in read(hostlog), 15) is not None,
                      "a block edit from the browser reaches the host (%s)" % mode)
                page.screenshot(path=os.path.join(args.logs, "page-%s.png" % mode))
            hlog = read(hostlog)
            check("webrtc: offer from str:edifice-" in hlog, "the host took the browser's offer")
            route = "relayed (TURN)" if args.relay else "direct"
            check(re.search(r"webrtc: connected with str:edifice-\S+, encrypted \(DTLS\), " + re.escape(route), hlog),
                  "an encrypted connection on the host side, %s" % route)
            m = re.search(r"webrtc: offer from \S+ \(as (0\.1\.\d+\.\d+)\)", hlog)
            check(bool(m) and " requests to connect" in hlog,
                  "the host saw the browser come from a made-up 0.1.x.y address")
            check("join cookie" not in hlog, "no join cookie asked (DTLS proved the address)")

            def two():
                s = listed()
                return s and s.get("players") == 2
            check(wait_for("the lobby lists 2 players", two, 30) is not None, "the host's renewal says 2 players")
            errors[:] = [e for e in errors if "pointer lock" not in e]
            check(not errors, "no page error")
            with open(pagelog, "w") as f:
                f.write("\n".join(log))
            browser.close()
    finally:
        httpd.shutdown()
        for p in procs:
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
                try:
                    p.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        for name in (hostlog,):
            text = read(name)
            if "ERROR: AddressSanitizer" in text or "runtime error:" in text:
                check(False, "sanitizer report in " + name)
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("%d failure(s); logs in %s" % (failures, args.logs))
        sys.exit(1)
    print("webrtc (%s): all tests passed; logs in %s" % (mode, args.logs))


if __name__ == "__main__":
    main()
