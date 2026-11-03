#!/usr/bin/env python3
"""Hits of a shooter on a moving target it sees (docs/M16.md, phase 5):
    python3 tests/net_hitreg.py <Edifice binary or Edifice.app> [--netsim 100,20,2] [--keep DIR]

Two instances of the game on this machine, as tests/net_sync.py (fresh user folders,
no lobby, servervisibility=lan, ports 37420/37421, HK_NETSIM on both): the host is
the target, strafing left and right at full speed (HK_PADTEST), always facing the
shooter; the client is the shooter, 6 units away (HK_SPAWNAT), standing, turned
each tick to where it draws the target and holding the trigger (HK_AIMBOT): what a
player who aims well does with what it sees. The server writes each shot it fires
for the client and each hit it decides (HK_HITLOG); bullets do no damage here (the
target never dies) nor push it away (HK_NOPUSH), and the hitbox has no tolerance
(ac_hittolerance 0).

Twice: with the server's rewind to the tick the shooter drew (FireTick, at most
ac_lagcomp), and without (ac_lagcomp 0). Passes if, during the strafing, the first
hits at least MINHITS of its shots and the second at most MAXMISSRATE of what the
first hit: the target is drawn some 300 ms in the past at 100,20,2 (the round trip
and the 100 ms of interpolation), more than a unit at its speed.
Local only (two windows), not in the CI. Linux needs a display (xvfb-run)."""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from net_sync import executable, user_folder  # noqa: E402

MINHITS = 0.75      # of the shots, with the rewind
MAXRATIO = 0.5      # without it: at most this much of the hits with it
START, LEG, LEGS = 900, 90, 14   # host frames (60 fps): strafing from START, LEGS legs of LEG
SPAWN = "800,806,0,-6"           # the host at 800,806, the client 6 units on z, 3.6 higher (HK_SPAWNAT): a clear shot down


def host_script():
    steps = ["%d:log" % (START - 20)]
    for k in range(LEGS):
        steps.append("%d:axis:leftx:%d" % (START + k * LEG, 1 if k % 2 == 0 else -1))
    end = START + LEGS * LEG
    steps += ["%d:axis:leftx:0" % end, "%d:log" % (end + 10)]
    return ";".join(steps), START - 20, end + 10


def run(exe, netsim, rewind, timeout):
    """the host's log (text)"""
    script, first, last = host_script()
    vars_ = "props=0 ac_hittolerance=0" + ("" if rewind else " ac_lagcomp=0")
    procs, logs, dirs = [], [], []
    for role in ("host", "client"):
        d = user_folder()
        dirs.append(d)
        env = dict(os.environ)
        for k in ("HK_AUTOSTART", "HK_JOIN", "HK_NETSIM", "HK_FIXEDCLOCK", "HK_AC_VARS", "HK_TEST_PORT",
                  "HK_PADTEST", "HK_AIMBOT", "HK_SPAWNAT", "HK_CONSOLE", "HK_CONSOLE_RUN"):
            env.pop(k, None)
        env.update(SDL_AUDIODRIVER="dummy", HK_PLAY="1", HK_SHOT_FRAME="99999999", HK_USERDIR=d,
                   HK_SHOT=os.path.join(d, "unused.ppm"), HK_MAXFPS="60")
        if netsim != "0":
            env["HK_NETSIM"] = netsim
        if role == "host":
            env.update(HK_AUTOSTART="1", HK_SEED="42", HK_AC_VARS=vars_, HK_SPAWNAT=SPAWN, HK_HITLOG="1",
                       HK_AIMBOT="1", HK_PADTEST=script, HK_NOPUSH="1",
                       HK_CONSOLE_RUN="lowbulletdamage 0;highbulletdamage 0")
        else:
            env.update(HK_JOIN="127.0.0.1", HK_SEED="43", HK_AIMBOT="0,fire")
        log = os.path.join(d, "log.txt")
        logs.append(log)
        with open(log, "wb") as f:
            procs.append(subprocess.Popen([exe], stdout=f, stderr=subprocess.STDOUT, env=env,
                                          stdin=subprocess.DEVNULL, cwd=d))
        if role == "host":
            time.sleep(2)   # listening before the client asks
    want = "padtest: frame %d " % last
    end = time.time() + timeout
    try:
        while time.time() < end and all(p.poll() is None for p in procs):
            if want in open(logs[0], "rb").read().decode("utf-8", "replace"):
                break
            time.sleep(0.5)
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
            p.wait()
    texts = [open(l, "rb").read().decode("utf-8", "replace") for l in logs]
    for d in dirs:
        import shutil
        shutil.rmtree(d, ignore_errors=True)
    return texts, first, last


def count(host, first, last):
    """shots, hits and the shots' rewinds (ticks) between the host's padtest frames
    first and last"""
    a = host.find("padtest: frame %d " % first)
    b = host.find("padtest: frame %d " % last)
    if a < 0 or b < 0:
        return None
    part = host[a:b]
    shots = [float(m.group(1)) for m in re.finditer(r"^hitlog shot 1 type [13] lag (-?[\d.]+)$", part, re.M)]
    hits = len(re.findall(r"^hitlog hit 1 0$", part, re.M))
    return len(shots), hits, shots


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--netsim", default="100,20,2")
    ap.add_argument("--timeout", type=float, default=150)
    ap.add_argument("--keep", help="folder to keep the logs in")
    a = ap.parse_args()
    exe = os.path.abspath(executable(a.binary))
    results = {}
    for rewind in (True, False):
        name = "rewind" if rewind else "no rewind (ac_lagcomp 0)"
        print("== HK_NETSIM=%s, %s" % (a.netsim, name), flush=True)
        for attempt in range(3):
            (host, client), first, last = run(exe, a.netsim, rewind, a.timeout)
            c = count(host, first, last)
            if c and c[0] >= 50:
                break
            # HK_JOIN asks once: its request or the answer lost (HK_NETSIM), it never joins
            print("  no game (%s shots), again" % (c[0] if c else "no"), flush=True)
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            tag = "rewind" if rewind else "norewind"
            open(os.path.join(a.keep, "host-%s.log" % tag), "w").write(host)
            open(os.path.join(a.keep, "client-%s.log" % tag), "w").write(client)
        if not c or c[0] < 50:
            print("  FAIL not enough shots during the strafing: %s" % (str(c[:2]) if c else "no game"))
            return 1
        shots, hits, lags = c
        lags.sort()
        results[rewind] = hits / shots
        print("  %d shots, %d hits (%.0f%%); rewind p50 %.1f ticks, max %.1f" % (
            shots, hits, 100 * hits / shots, lags[len(lags) // 2], lags[-1]), flush=True)
    ok = True
    if results[True] < MINHITS:
        print("FAIL with the rewind: %.0f%% of the shots hit, at least %.0f%% expected" % (
            100 * results[True], 100 * MINHITS))
        ok = False
    else:
        print("ok   with the rewind: %.0f%% of the shots hit" % (100 * results[True]))
    if a.netsim == "0":
        # no latency: the target is drawn only the interpolation's 100 ms in the past,
        # half a unit at its speed, about the size of the hitbox
        print("     without the rewind: %.0f%% hit (HK_NETSIM 0: not checked)" % (100 * results[False]))
    elif results[False] > results[True] * MAXRATIO:
        print("FAIL without the rewind: %.0f%% hit, not clearly fewer than with it" % (100 * results[False]))
        ok = False
    else:
        print("ok   without the rewind: %.0f%% hit (the target is drawn in the past)" % (100 * results[False]))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
