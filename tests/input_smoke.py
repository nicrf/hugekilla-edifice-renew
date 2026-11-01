#!/usr/bin/env python3
"""Plays scripted gamepad input in a built game and checks what it did:
    python3 tests/input_smoke.py <Edifice binary or Edifice.app> [--out dir] [--only name]

Each scene runs in a fresh user folder (HK_USERDIR: CRLF copies of data/*.ini, net.ini
with no lobby, servervisibility=lan and ports 37320/37321, so nothing is announced or
contacted), with HK_PADTEST: presses and stick moves at given frames, through SDL's
virtual controller, down the same path as a real pad's (src/platform/gamepad.cpp).
The game prints its state at the script's "log" steps ("padtest: ..."), which the
scene's checks read, and saves frames at its "shot" steps (HK_FIXEDCLOCK=16), kept
in --out as PNG. Linux needs a display (xvfb-run). Not in the CI (it moves the
player around for real; render_smoke.py is the CI's)."""
import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pixel_diff import read_ppm  # noqa: E402

DATA = os.path.join(os.path.dirname(HERE), "data")


def executable(path):
    if path.endswith(".app"):
        return os.path.join(path, "Contents", "MacOS", "Edifice")
    return path


def user_folder():
    """data/*.ini, CRLF as the game writes them; net.ini kept off the network"""
    folder = tempfile.mkdtemp(prefix="edifice-input-")
    for name in os.listdir(DATA):
        if not name.endswith(".ini"):
            continue
        with open(os.path.join(DATA, name), "rb") as f:
            lines = f.read().decode("latin-1").replace("\r\n", "\n").split("\n")
        if name == "net.ini":
            out = []
            for line in lines:
                if line.startswith("lobby="):
                    line = "lobby="
                elif line.startswith("servervisibility="):
                    line = "servervisibility=lan"
                elif line.startswith("infosendport="):
                    line = "infosendport=37320"
                elif line.startswith("datasendport="):
                    line = "datasendport=37321"
                out.append(line)
            lines = out
        with open(os.path.join(folder, name), "wb") as f:
            f.write("\r\n".join(lines).encode("latin-1"))
    return folder + os.sep


def write_png(ppm, png):
    w, h, px = read_ppm(ppm)
    rows = b"".join(b"\0" + bytes(px[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(png, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


LOG = re.compile(r"padtest: frame (\d+) (.*)")


def parse_logs(text):
    """frame -> dict of the logged values"""
    states = {}
    for line in text.splitlines():
        m = LOG.match(line)
        if not m:
            continue
        words = m.group(2).split()
        st, i = {}, 0
        while i < len(words):
            key = words[i]
            vals = []
            i += 1
            while i < len(words) and re.match(r"^-?[0-9.]+$", words[i]):
                vals.append(float(words[i]))
                i += 1
            st[key] = vals[0] if len(vals) == 1 else vals
        states[int(m.group(1))] = st
    return states


def run(exe, script, env_extra, out, timeout=60):
    userdir = user_folder()
    env = dict(os.environ)
    env.update(env_extra)
    # HK_SHOT never reached: no VSync, and the frames keep coming without the focus
    env.update(HK_USERDIR=userdir, HK_SEED="42", SDL_AUDIODRIVER="dummy", HK_FIXEDCLOCK="16",
               HK_PADTEST=script, HK_CONSOLE_LOG="1", HK_PADSHOTS=out,
               HK_SHOT=os.path.join(userdir, "unused.ppm"), HK_SHOT_FRAME="999999")
    last = max(int(s.split(":")[0]) for s in script.split(";") if s)
    logpath = os.path.join(userdir, "log.txt")
    with open(logpath, "wb") as f:
        p = subprocess.Popen([exe], stdout=f, stderr=subprocess.STDOUT, env=env,
                             stdin=subprocess.DEVNULL, cwd=userdir)
    end = time.time() + timeout
    want = "padtest: frame %d " % last
    try:
        while time.time() < end and p.poll() is None:
            with open(logpath, "rb") as f:
                text = f.read().decode("utf-8", "replace")
            if want in text:
                time.sleep(0.3)
                break
            time.sleep(0.2)
    finally:
        exited = p.poll()
        if exited is None:
            p.kill()
        p.wait()
    with open(logpath, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    shutil.rmtree(userdir, ignore_errors=True)
    return text, exited


def horizontal(a, b):
    return math.hypot(b["pos"][0] - a["pos"][0], b["pos"][2] - a["pos"][2])


def scene_game(exe, out):
    """in a solo game: walk, look, change weapon, fire, open the build menu, pause"""
    s = ("300:log;"
         "305:axis:lefty:-1;395:log;396:axis:lefty:0;"
         "420:log;425:axis:rightx:1;455:axis:rightx:0;460:log;"
         "470:axis:righty:-1;490:axis:righty:0;495:log;"
         "500:press:rightshoulder;520:log;"
         "530:down:righttrigger;540:log;545:up:righttrigger;550:log;"
         "560:down:lefttrigger;570:log;575:up:lefttrigger;580:log;"
         "590:axis:leftx:.5;650:log;651:axis:leftx:0;"
         "660:press:y;670:press:dpdown;676:press:dpdown;678:shot:padbuildmenu;680:log;690:press:b;710:log;"
         "720:press:start;730:press:dpdown;738:shot:padpause;740:log;750:press:start;770:log;"
         "780:axis:lefty:1;790:log;800:unplug;810:log;820:plug;860:log")
    text, exited = run(exe, s, dict(HK_AUTOSTART="1", HK_PLAY="1"), out)
    st = parse_logs(text)
    checks = []
    def check(ok, what):
        checks.append((ok, what))
    need = [300, 395, 420, 460, 495, 520, 540, 550, 570, 580, 650, 680, 710, 740, 770, 790, 810, 860]
    missing = [f for f in need if f not in st]
    check(not missing, "all logs (missing %s)" % missing)
    if missing:
        return checks, text
    check(st[300]["playstate"] == 10 and st[300]["ingame"] == 1, "playing at frame 300")
    d = horizontal(st[300], st[395])
    check(d > 2, "left stick forward walks (%.2f units in 90 frames)" % d)
    check(st[395]["up"] == 1 and st[395]["left"] == 0, "the forward flag is on while walking")
    check(st[420]["up"] == 0, "and off when let go")
    yaw = (st[460]["yaw"] - st[420]["yaw"] + 180) % 360 - 180
    check(abs(yaw) > 20, "right stick turns (%.1f degrees in 30 frames)" % yaw)
    check(st[495]["tilt"] != st[460]["tilt"], "right stick up/down tilts the view")
    check(st[520]["weapon"] != st[495]["weapon"], "RB changes the weapon (%d -> %d)" % (st[495]["weapon"], st[520]["weapon"]))
    check(st[540]["firing"] == 1 and st[550]["firing"] == 0, "RT fires while held")
    check(st[570]["jetpack"] == 1 and st[580]["jetpack"] == 0, "LT is the jetpack while held")
    d = horizontal(st[580], st[650])
    check(st[650]["right"] == 1 and st[650]["up"] == 0 and d > .3,
          "half a stick to the right strafes, slower (%.2f units in 60 frames)" % d)
    check(st[680]["buildmenu"] == 1 and st[680]["focus"] > 0, "Y opens the build menu, with focus areas")
    check(st[710]["buildmenu"] == 0, "B closes it")
    check(st[740]["ingame"] == 0 and st[770]["ingame"] == 1, "Start pauses and resumes")
    check(st[790]["down"] == 1 and st[810]["down"] == 0, "unplugged while walking back: nothing stays held")
    check(st[810]["pads"] == 0 and st[860]["pads"] == 1, "unplugged and plugged again")
    check("Gamepad connected" in text and "Gamepad disconnected" in text, "hot-plug messages in the console")
    check(exited is None, "the game kept running")
    return checks, text


def scene_spawn(exe, out):
    """the spawn screen: A plays"""
    s = "250:log;260:press:a;300:log"
    text, exited = run(exe, s, dict(HK_AUTOSTART="1"), out)
    st = parse_logs(text)
    checks = []
    ok = 250 in st and 300 in st
    checks.append((ok and st[250]["playstate"] == 11 and st[300]["playstate"] == 10,
                   "the spawn screen, then A spawns"))
    return checks, text


def scene_menus(exe, out):
    """main menu -> Options -> Gamepad tab with the D-pad and A; a value stepped"""
    steps = ("60:log;80:press:dpdown;90:press:dpdown;100:press:dpdown;110:press:dpdown;120:log;"
             "130:press:a;150:log;160:press:dpright;170:press:dpright;180:press:dpright;190:press:a;"
             "200:press:dpdown;210:press:dpright;225:shot:padoptions;230:log;240:press:b;260:log;"
             "122:shot:padmenu")
    checks = []
    text, exited = run(exe, steps, {}, out)
    st = parse_logs(text)
    ok = all(f in st for f in (60, 120, 150, 230, 260))
    checks.append((ok, "all logs"))
    if ok:
        checks.append((st[60]["options"] == 0 and st[150]["options"] == 1, "D-pad down to Options, A opens it"))
        checks.append((st[230]["options"] == 1 and st[260]["options"] == 0, "B closes it"))
    text2 = ""
    # Server Setup and the join list, focus moved with the stick
    text3, _ = run(exe, "60:axis:lefty:1;66:axis:lefty:0;80:axis:lefty:1;86:axis:lefty:0;90:press:dpright;98:shot:padserver;100:log",
                   dict(HK_MENU="server"), out)
    st3 = parse_logs(text3)
    checks.append((100 in st3 and st3[100]["setup"] == 1 and st3[100]["focus"] >= 10, "Server Setup registers its rows"))
    text4, _ = run(exe, "60:press:dpdown;70:press:dpdown;80:press:dpdown;90:press:dpdown;100:press:a;125:shot:padjoin;130:log",
                   dict(HK_MENU="joindemo"), out)
    st4 = parse_logs(text4)
    checks.append((130 in st4 and st4[130]["join"] == 1, "the join screen stays open after A on a row (selects)"))
    return checks, text + text2 + text3 + text4


def scene_build(exe, out):
    """the build menu, moved through with the D-pad"""
    s = "250:press:dpdown;260:press:dpdown;270:press:dpright;280:press:dpdown;295:shot:padbuild;300:log"
    text, exited = run(exe, s, dict(HK_MENU="build"), out)
    st = parse_logs(text)
    return [(300 in st and st[300]["buildmenu"] == 1 and st[300]["focus"] > 3, "the build menu with focus areas")], text


SCENES = [("game", scene_game), ("spawn", scene_spawn), ("menus", scene_menus), ("build", scene_build)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("game")
    ap.add_argument("--out", default=None, help="folder for the captures (default: a temporary one)")
    ap.add_argument("--only", default=None)
    args = ap.parse_args()
    exe = executable(os.path.abspath(args.game))
    out = os.path.abspath(args.out or tempfile.mkdtemp(prefix="edifice-input-shots-"))
    os.makedirs(out, exist_ok=True)
    failed = 0
    for name, fn in SCENES:
        if args.only and name != args.only:
            continue
        checks, text = fn(exe, out)
        bad = [w for ok, w in checks if not ok]
        for ok, what in checks:
            print("%s %s: %s" % ("ok  " if ok else "FAIL", name, what))
        if bad:
            failed += 1
            print("    -- log (tail):\n    " + "\n    ".join(text.splitlines()[-25:]))
    for f in sorted(os.listdir(out)):
        if f.endswith(".ppm"):
            write_png(os.path.join(out, f), os.path.join(out, f[:-4] + ".png"))
            os.remove(os.path.join(out, f))
    print("captures in " + out)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
