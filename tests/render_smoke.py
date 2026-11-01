#!/usr/bin/env python3
"""Draws a few frames with a built game and checks the renderer (roadmap M13):
    python3 tests/render_smoke.py <Edifice binary or Edifice.app> [--out dir] [--ref dir]

Each scene runs in a fresh user folder (HK_USERDIR; net.ini with servervisibility=lan
and no lobby, so nothing is announced), saves one frame (HK_SHOT, HK_FIXEDCLOCK=16)
and is stopped. A scene fails when the game exits or saves nothing within 60 s, when
its log has a sokol_gfx error or validation message (a Debug build validates every
call; warnings are only shown), or when the frame is one flat color. --ref compares with the same captures of
another build (tests/pixel_diff.py), for information: what the CI's runners draw
(software GL, WARP, a virtual GPU) differs from a real GPU. Linux needs a display
(xvfb-run). Used by the CI's sokol job."""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pixel_diff import read_ppm, compare  # noqa: E402

DATA = os.path.join(os.path.dirname(HERE), "data")
SCENES = [
    ("menu", dict(HK_SHOT_FRAME="200")),
    ("options", dict(HK_MENU="options", HK_SHOT_FRAME="200")),
    ("credits", dict(HK_MENU="credits", HK_SHOT_FRAME="200")),
    # not HK_CONSOLE=1: on the CI's runners it sometimes never gets to its frame (any
    # renderer); the console is drawn over the game and in the menus anyway
    ("spawn", dict(HK_AUTOSTART="1", HK_SHOT_FRAME="300", HK_FIXEDCLOCK="16")),
    ("game", dict(HK_AUTOSTART="1", HK_PLAY="1", HK_SHOT_FRAME="600", HK_FIXEDCLOCK="16")),
]


def executable(path):
    if path.endswith(".app"):
        return os.path.join(path, "Contents", "MacOS", "Edifice")
    return path


def net_ini(folder):
    """data/net.ini with servervisibility=lan and no lobby, CRLF as the game writes it"""
    with open(os.path.join(DATA, "net.ini"), "rb") as f:
        lines = f.read().decode("latin-1").replace("\r\n", "\n").split("\n")
    out = []
    for line in lines:
        if line.startswith("lobby="):
            line = "lobby="
        elif line.startswith("servervisibility="):
            line = "servervisibility=lan"
        out.append(line)
    if "servervisibility=lan" not in out:
        out.append("servervisibility=lan")
    with open(os.path.join(folder, "net.ini"), "wb") as f:
        f.write("\r\n".join(out).encode("latin-1"))


def run_scene(exe, name, env_extra, outdir):
    shot = os.path.join(outdir, name + ".ppm")
    if os.path.exists(shot):
        os.remove(shot)
    userdir = tempfile.mkdtemp(prefix="edifice-render-")
    net_ini(userdir)
    env = dict(os.environ)
    env.update(env_extra)
    env.update(HK_USERDIR=userdir + os.sep, HK_SEED="42", SDL_AUDIODRIVER="dummy", HK_SHOT=shot,
               HK_CONSOLE_LOG="1")   # the console on stderr: its first line names the renderer
    log = os.path.join(outdir, name + ".log")
    with open(log, "wb") as f:
        p = subprocess.Popen([exe], stdout=f, stderr=subprocess.STDOUT, env=env,
                             stdin=subprocess.DEVNULL, cwd=userdir)
    end = time.time() + 60
    try:
        while time.time() < end and p.poll() is None:
            with open(log, "rb") as f:
                if b"saved " in f.read():
                    time.sleep(0.5)   # the rest of that frame's messages
                    break
            time.sleep(0.2)
    finally:
        exited = p.poll()
        if exited is None:
            p.kill()
        p.wait()
        shutil.rmtree(userdir, ignore_errors=True)
    with open(log, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    problems, notes = [], []
    if exited is not None:
        problems.append("exited with code %d" % exited)
    for line in text.splitlines():
        # sokol_gfx's errors (validation included; its warnings are informative, like
        # the D3D11 feature level) and the window side's failures (rcontext.h)
        if line.startswith(("sokol_gfx panic", "sokol_gfx error", "SDL_GL_CreateContext", "SDL_Metal_CreateView",
                            "no Metal device", "D3D11CreateDeviceAndSwapChain", "SDL_GetWindowWMInfo",
                            "SDL_CreateWindow")):
            problems.append(line)
        elif line.startswith("sokol_gfx") and line not in notes:
            notes.append(line)
    if not os.path.isfile(shot):
        problems.append("no frame saved within 60 s")
    else:
        w, h, px = read_ppm(shot)
        # one flat color: nothing drawn (a black window, or a present that never came)
        if px[:3] * (w * h) == px:
            problems.append("the frame is one flat color %s" % tuple(px[:3]))
    return problems, notes, text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("game")
    ap.add_argument("--out", default=None, help="folder for the captures and logs (default: a temporary one)")
    ap.add_argument("--ref", default=None, help="captures of another build, to compare with")
    args = ap.parse_args()
    exe = executable(os.path.abspath(args.game))
    outdir = os.path.abspath(args.out or tempfile.mkdtemp(prefix="edifice-shots-"))
    os.makedirs(outdir, exist_ok=True)
    print("%s: captures in %s" % (platform.system(), outdir))
    failed = 0
    for name, env in SCENES:
        problems, notes, text = run_scene(exe, name, env, outdir)
        first = next((l[9:] for l in text.splitlines() if l.startswith("console: Edifice ")), "")
        status = "ok" if not problems else "FAILED"
        print("%-8s %s %s" % (name, status, first))
        for pr in problems[:10] + notes[:5]:
            print("    " + pr)
        if problems:
            failed += 1
            print("    -- log (tail):\n    " + "\n    ".join(text.splitlines()[-15:]))
        if args.ref and os.path.isfile(os.path.join(args.ref, name + ".ppm")) and not problems:
            try:
                diff, total, biggest = compare(os.path.join(args.ref, name + ".ppm"),
                                               os.path.join(outdir, name + ".ppm"), 8)
                print("    vs %s: %.3f%% of the pixels differ by more than 8, at most by %d" %
                      (args.ref, 100.0 * diff / max(total, 1), biggest))
            except ValueError as e:
                print("    vs %s: %s" % (args.ref, e))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
