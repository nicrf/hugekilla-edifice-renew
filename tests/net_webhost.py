#!/usr/bin/env python3
"""A game hosted in a browser, joined by a browser and by the desktop game over
WebRTC (roadmap M14 task 3, "Héberger depuis le navigateur" in docs/M7.md), end to
end on one machine:
    python3 tests/net_webhost.py <Edifice binary or .app> <edifice-lobby binary> [build/web]
                                 [--relay] [--headed] [--logs DIR]
Needs the web build (python3 build.py --web), a native build with
GameNetworkingSockets and libdatachannel (python3 build.py: EDIFICE_WEBRTC) and
Playwright (pip install playwright; playwright install chromium-headless-shell).

  1. a local edifice-lobby (server/): plain HTTP on 127.0.0.1 with its WebSocket
     signaling relay, CORS for the page's origin, -allow-private, STUN/TURN on this
     machine's LAN address (neither libjuice nor Chromium use loopback for ICE);
  2. the host: the web build in headless Chromium, its net.ini pointing to that
     lobby with servervisibility=public, starts a server (HK_AUTOSTART). It gets a
     session, keeps the relay's WebSocket open and is listed with "webrtc": true
     and "host": "web" (lobby API 5), a banner asking to keep the tab open;
  3. a second browser (its own storage, so its own net.ini and identity) joins by
     the game code (HK_LOBBYJOIN, HK_PLAY): its offer, the host's answer and the
     candidates over the relay, then the DataChannel carries the game protocol;
     a shot and a block edit must reach the host (HK_NET_EVENTS, in its console);
  4. the desktop game, with its own user folder (HK_USERDIR, a CRLF net.ini: the
     local lobby, ports 37320/37321), joins the same code: it sees "host": "web"
     and offers a DataChannel itself (libdatachannel); it must enter the game and
     get the land, and the lobby must list 3 players;
  5. the host's frame rate with both players in is printed (WEBHOSTMAXPLAYERS).
--relay: p2p=relay everywhere (TURN relay candidates only: every DataChannel goes
through the lobby's TURN server). Nothing is contacted but this machine (the
public lobby never: every net.ini here names the local one).
NAT: everything runs on one machine, so there is no NAT between the players. On
Linux the native game needs a display (xvfb-run). Local test, not in the CI (the
netplay tests run locally, see BUILD.md)."""
import argparse
import functools
import http.server
import json
import os
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
CLIENTPORT = 37320   # the desktop client's; not 27020: a real game may run on this machine
HOSTPORT = 37330     # what the browser host announces (it binds nothing)
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
    ap.add_argument("--relay", action="store_true", help="TURN relay candidates only, everywhere")
    ap.add_argument("--headed", action="store_true")
    ap.add_argument("--logs", default=os.path.join(ROOT, "build", "webhost-logs"))
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
    work = tempfile.mkdtemp(prefix="edifice-webhost-")
    address = lan_address()
    check(not address.startswith("127."), "LAN address for ICE and TURN: " + address)
    httpd, origin = serve(args.site)
    lobbyport, turnport = free_port(), free_port(socket.SOCK_DGRAM)
    lobbyurl = "http://127.0.0.1:%d" % lobbyport
    mode = "relay" if args.relay else "direct"
    lobbylog = os.path.join(args.logs, "lobby-%s.log" % mode)
    clientlog = os.path.join(args.logs, "desktop-%s.log" % mode)
    route = "relayed (TURN)" if args.relay else "direct"
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

        def listed():
            try:
                with urllib.request.urlopen(lobbyurl + "/v1/servers?protocol=0", timeout=2) as r:
                    for s in json.loads(r.read())["servers"]:
                        if s["name"] == "Browser test host" and s.get("identity"):
                            return s
            except (OSError, ValueError, KeyError):
                pass
            return None

        with sync_playwright() as p:
            browser = p.chromium.launch(headless=not args.headed,
                                        args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader",
                                              "--ignore-gpu-blocklist", "--autoplay-policy=no-user-gesture-required"])
            pages = {}

            def open_page(name, ini):
                # a context each: its own IndexedDB, so its own net.ini (and identity)
                page = browser.new_context(viewport={"width": 1024, "height": 700}).new_page()
                log, errors = [], []
                page.on("console", lambda m: log.append(m.text))
                page.on("pageerror", lambda e: errors.append(str(e)))
                pages[name] = (page, log, errors)
                if not PAGE:
                    PAGE.append(page)

                def load(query=""):
                    page.goto(origin + "/" + query)
                    page.wait_for_function("document.getElementById('status').classList.contains('hidden')",
                                           timeout=60000)
                load()
                # the page's net.ini: the local lobby (never the public one), and p2p
                page.evaluate("""(extra) => new Promise(r => {
                    const f = '/user/net.ini';
                    let ini = FS.readFile(f, {encoding: 'utf8'}).replace(
                        /\\r?\\n?^(lobby|name|p2p|servername|servervisibility|infosendport|datasendport)=.*$/mg, '');
                    FS.writeFile(f, ini + extra);
                    FS.syncfs(false, r);
                })""", "".join("\r\n%s=%s" % kv for kv in ini) + "\r\n")
                return page, log, errors, load

            relay = [("p2p", "relay")] if args.relay else []
            host, hostlog, hosterrors, hostload = open_page("host", [
                ("lobby", lobbyurl), ("name", "WebHost"), ("servername", "Browser test host"),
                ("servervisibility", "public"), ("infosendport", HOSTPORT), ("datasendport", HOSTPORT + 1)] + relay)
            hostload("?HK_AUTOSTART=1&HK_CONSOLE_LOG=1&HK_NET_EVENTS=1")
            listing = wait_for("the browser host is listed with its identity", listed, 60)
            if not listing:
                return
            check(listing.get("webrtc") is True and listing.get("host") == "web",
                  "listed as a browser host (webrtc, host: web)")
            check(listing.get("maxplayers") == 8, "a browser host takes 8 players (%s)" % listing.get("maxplayers"))
            check(host.evaluate("!!document.getElementById('hostbanner') && "
                                "document.getElementById('hostbanner').style.display !== 'none'"),
                  "the host page asks to keep its tab open")

            def logged(log, text):
                return lambda: any(text in l for l in log)
            check(wait_for("the host keeps the relay open", logged(hostlog, "signaling: connected to ws://127.0.0.1:%d"
                                                                    % lobbyport), 20) is not None,
                  "the host signals over the lobby's WebSocket relay")

            # a browser joins
            guest, guestlog, guesterrors, guestload = open_page("guest", [
                ("lobby", lobbyurl), ("name", "WebGuest")] + relay)
            guestload("?HK_LOBBYJOIN=%s&HK_PLAY=1&HK_CONSOLE_LOG=1" % listing["id"])
            entered = wait_for("the browser guest enters the game (%s)" % mode, logged(guestlog, "entering game"), 60)
            check(any("webrtc: DataChannel open with " + listing["identity"] in l for l in guestlog),
                  "the guest's DataChannel goes to the listed identity (%s)" % mode)
            if entered:
                check(wait_for("the land arrives", logged(guestlog, "received landinfo"), 30) is not None,
                      "the browser guest got the land (%s)" % mode)
                guest.mouse.move(512, 350)
                states = []
                end = time.time() + 30
                while time.time() < end and states[-6:].count(10) < 6:
                    guest.mouse.down()
                    guest.wait_for_timeout(300)
                    guest.mouse.up()
                    guest.wait_for_timeout(300)
                    states.append(guest.evaluate("Module._HKWebPlayState()"))
                check(10 in states, "the browser guest spawned and played (%s)" % mode)
                guest.evaluate("Module._HKWebSendBlock()")
                check(wait_for("a shot reaches the host", logged(hostlog, "net: shot from player 1"), 15)
                      is not None, "a shot from the browser guest reaches the browser host (%s)" % mode)
                check(wait_for("a block edit reaches the host", logged(hostlog, "net: blocks from player 1"), 15)
                      is not None, "a block edit from the browser guest reaches the host (%s)" % mode)
            check(any(re.search(r"webrtc: offer from str:edifice-\S+ \(as 0\.1\.\d+\.\d+\)", l) for l in hostlog),
                  "the host took the guest's offer under a made-up 0.1.x.y address")
            check(wait_for("the host tells the route", lambda: any(
                re.search(r"webrtc: connected with str:edifice-\S+, encrypted \(DTLS\), " + re.escape(route), l)
                for l in hostlog), 10) is not None, "the host's side of the guest is %s" % route)

            # the desktop game joins
            clientdir = os.path.join(work, "desktop") + os.sep
            os.makedirs(clientdir)
            with open(os.path.join(clientdir, "net.ini"), "w", newline="\r\n") as f:
                f.write("infosendport=%d\ndatasendport=%d\nname=Desktop\nlobby=%s\nservervisibility=lan\n"
                        % (CLIENTPORT, CLIENTPORT + 1, lobbyurl))
            env = dict(os.environ, HK_USERDIR=clientdir, HK_LOBBYJOIN=listing["id"], HK_PLAY="1",
                       HK_CONSOLE_LOG="1", SDL_AUDIODRIVER="dummy", ASAN_OPTIONS="detect_leaks=0",
                       # HK_SHOT turns VSync off, whose swap waits forever while the
                       # display sleeps (macOS); the frame is never reached
                       HK_SHOT=os.path.join(work, "never.ppm"), HK_SHOT_FRAME="100000000")
            if args.relay:
                env["HK_P2P"] = "relay"
            procs.append(subprocess.Popen([game], stdout=open(clientlog, "w"), stderr=subprocess.STDOUT, env=env))
            check(wait_for("the desktop game enters the game (%s)" % mode,
                           lambda: "entering game" in read(clientlog), 60) is not None,
                  "the desktop game enters the browser's game (%s)" % mode)
            clog = read(clientlog)
            check("webrtc: connecting to " + listing["identity"] in clog,
                  "the desktop game offers WebRTC to the browser host (not GameNetworkingSockets)")
            # the desktop game offers, so its ICE agent (libjuice) nominates the pair:
            # on one machine it may keep the TURN pair it found first, which is
            # still a working route; only --relay insists on the relay
            m = re.search(r"webrtc: connected with " + re.escape(listing["identity"]) +
                          r", encrypted \(DTLS\), (direct|relayed \(TURN\))", clog)
            check(m and (not args.relay or m.group(1) == route),
                  "the desktop game's DataChannel is encrypted, %s" % (m.group(1) if m else "no route"))
            check(wait_for("the land arrives", lambda: "received landinfo" in read(clientlog), 30) is not None,
                  "the desktop game got the land (%s)" % mode)
            check(any(re.search(r"Desktop at 0\.1\.\d+\.\d+ requests to connect", l) for l in hostlog),
                  "the host saw the desktop game come from a made-up 0.1.x.y address")
            check(not any("join cookie" in l for l in hostlog), "no join cookie asked (DTLS proved the addresses)")

            def three():
                s = listed()
                return s and s.get("players") == 3
            check(wait_for("the lobby lists 3 players", three, 30) is not None, "the host's renewal says 3 players")

            # what hosting costs the page: its frame rate with both players in
            fps = host.evaluate("""() => new Promise(r => {
                let n = 0; const t0 = performance.now();
                const f = () => { n++; if (performance.now() - t0 < 3000) requestAnimationFrame(f);
                                  else r(n * 1000 / (performance.now() - t0)); };
                requestAnimationFrame(f);
            })""")
            print("     host page: %.0f frames per second with 2 players in (headless, SwiftShader)" % fps)
            check(fps > 10, "the browser host keeps running (%.0f fps)" % fps)

            for name, (page, log, errors) in pages.items():
                page.screenshot(path=os.path.join(args.logs, "%s-%s.png" % (name, mode)))
                with open(os.path.join(args.logs, "%s-%s.log" % (name, mode)), "w") as f:
                    f.write("\n".join(log))
                errors[:] = [e for e in errors if "pointer lock" not in e]
                check(not errors, "no page error (%s)" % name)
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
        text = read(clientlog)
        if "ERROR: AddressSanitizer" in text or "runtime error:" in text:
            check(False, "sanitizer report in " + clientlog)
        shutil.rmtree(work, ignore_errors=True)
    if failures:
        print("%d failure(s); logs in %s" % (failures, args.logs))
        sys.exit(1)
    print("webhost (%s): all tests passed; logs in %s" % (mode, args.logs))


if __name__ == "__main__":
    main()
