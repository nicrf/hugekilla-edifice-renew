#!/usr/bin/env python3
"""Smoke test of the web build (roadmap M14) in headless Chromium.

    python3 tests/web_smoke.py [build/web] [--out DIR] [--headed]

Serves the built site (index.html, .js, .wasm, .data) on a local port, opens it
in Chromium through Playwright (pip install playwright; playwright install
chromium-headless-shell) and checks that:
  - the page loads data/ and starts (the status text goes away), without an abort;
  - the main menu draws (the canvas isn't one flat color);
  - a solo game starts (?HK_MENU=serverstart: Server Setup, then Start, as the
    player clicks it), the player spawns at a click and walks, the game keeps running;
  - the user folder is in IndexedDB (/user, net.ini copied there).
Screenshots go to --out (default build/web-smoke). Nothing is contacted but
127.0.0.1 (the menu and a solo game don't ask the lobby anything).
"""
import argparse
import base64
import functools
import http.server
import os
import socket
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def serve(site):
    class Quiet(http.server.SimpleHTTPRequestHandler):
        extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                              **{".wasm": "application/wasm", ".data": "application/octet-stream"})

        def log_message(self, *a):
            pass
    handler = functools.partial(Quiet, directory=site)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, "http://127.0.0.1:%d/" % port


def sample(page, path):
    """screenshot to path; the number of distinct colors (5 bits a channel) on a
    grid over it, decoded by the page (WebGL's canvas can't be read back after its
    frame, a screenshot can)"""
    png = page.screenshot(path=path)
    return page.evaluate("""async (b64) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + b64;
        await img.decode();
        const t = document.createElement('canvas');
        t.width = 64; t.height = 48;
        const g = t.getContext('2d');
        g.drawImage(img, 0, 0, 64, 48);
        const d = g.getImageData(0, 0, 64, 48).data;
        const seen = new Set();
        for (let i = 0; i < d.length; i += 4) seen.add((d[i] >> 3) + ',' + (d[i+1] >> 3) + ',' + (d[i+2] >> 3));
        return seen.size;
    }""", base64.b64encode(png).decode())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("site", nargs="?", default=os.path.join(ROOT, "build", "web"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "web-smoke"))
    ap.add_argument("--headed", action="store_true")
    args = ap.parse_args()
    if not os.path.isfile(os.path.join(args.site, "index.wasm")):
        sys.exit("no web build in %s (python3 build.py --web)" % args.site)
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        sys.exit("needs Playwright: pip install playwright && playwright install chromium-headless-shell")
    os.makedirs(args.out, exist_ok=True)
    httpd, url = serve(args.site)
    errors = []
    failed = []

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what, flush=True)
        if not ok:
            failed.append(what)

    with sync_playwright() as p:
        # WebGL2 without a GPU: SwiftShader, as on the CI runners
        browser = p.chromium.launch(headless=not args.headed,
                                    args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader",
                                          "--ignore-gpu-blocklist", "--autoplay-policy=no-user-gesture-required"])
        context = browser.new_context(viewport={"width": 1024, "height": 700})
        page = context.new_page()
        log = []
        page.on("console", lambda m: log.append("%s: %s" % (m.type, m.text)))
        page.on("pageerror", lambda e: errors.append(str(e)))

        def load(query=""):
            page.goto(url + query)
            # running once main() is in its loop: the status box is hidden
            page.wait_for_function("document.getElementById('status').classList.contains('hidden')",
                                   timeout=60000)

        # first load: the menu
        load()
        page.wait_for_timeout(1500)
        check(sample(page, os.path.join(args.out, "menu.png")) > 8, "the main menu draws")
        check(not errors, "no page error at startup")
        check(page.evaluate("FS.analyzePath('/user/net.ini').exists"), "net.ini copied to the user folder")
        # what is saved in /user is there after a reload (IndexedDB)
        page.evaluate("""() => new Promise(r => {
            FS.writeFile('/user/web_smoke.txt', 'kept');
            FS.syncfs(false, r);
        })""")

        # a solo game, through Server Setup's Start (HK_MENU=serverstart)
        load("?HK_MENU=serverstart&HK_SEED=42")
        check(page.evaluate("FS.analyzePath('/user/web_smoke.txt').exists"), "the user folder survives a reload")
        page.wait_for_timeout(5000)
        check(sample(page, os.path.join(args.out, "solo.png")) > 8, "a solo game draws (spawn screen)")
        frames = page.evaluate("""() => new Promise(r => {
            let n = 0; const t0 = performance.now();
            function f() { n++; if (performance.now() - t0 < 2000) requestAnimationFrame(f); else r(n); }
            requestAnimationFrame(f);
        })""")
        print("     %d animation frames in 2 s" % frames)
        # Play: a click anywhere, held a few frames as a player's (the game only
        # sees the shot if the button is still down at its next frame)
        page.mouse.move(512, 350)
        page.mouse.down()
        page.wait_for_timeout(150)
        page.mouse.up()
        page.wait_for_timeout(1500)
        check(sample(page, os.path.join(args.out, "solo-play.png")) > 8, "playing")
        check(page.evaluate("Module._HKWebPlayState()") == 10, "spawned in the game")
        page.keyboard.down("w")
        page.wait_for_timeout(1500)
        page.keyboard.up("w")
        check(sample(page, os.path.join(args.out, "solo-walk.png")) > 8, "still drawing after walking")
        # headless Chromium refuses pointer lock (no real window): not the game's
        errors[:] = [e for e in errors if "pointer lock" not in e]
        check(not errors, "no page error in the game")
        aborted = [l for l in log if "abort" in l.lower() or "Aborted" in l]
        check(not aborted, "the game didn't abort")
        browser.close()
    httpd.shutdown()
    for l in log[-40:]:
        print("  console " + l)
    for e in errors:
        print("  page error " + e)
    print("screenshots in " + args.out)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
