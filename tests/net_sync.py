#!/usr/bin/env python3
"""How far off the other players are drawn (docs/M16.md, phase 0):
    python3 tests/net_sync.py <Edifice binary or Edifice.app> [--netsim 100,20,2 ...] [--fps 60[,144]]
        [--vars "sv_authmove=0"] [--legacy]

Two instances of the game on this machine: a host (HK_AUTOSTART) and a client that
joins it (HK_JOIN=127.0.0.1), each in a fresh user folder (CRLF copies of
data/*.ini, net.ini with no lobby, servervisibility=lan, ports 37420/37421: nothing
is announced or contacted). Both play the same scripted walk (HK_PADTEST: walk,
turn, strafe, jetpack, jump, back...) and print where every player is drawn each
frame (HK_SYNCLOG), on the wall clock. For each direction (the client as the host
draws it, the host as the client draws it), against where that player really was
at the same moment:
 - error: distance between the two (mean, p95, max), in units (a player is ~2 high);
 - lag: the delay that best explains what is drawn (the true path shifted back by
   it), and the error left once shifted: what is noise rather than latency;
 - snaps: frames where the drawn player jumps more than SNAP units further than
   the true one moved in the same time (teleports, rubber bands), per second;
   and the same against the true path lag earlier (@lag/s): a player drawn late on
   purpose (docs/M16.md phase 4, 100 ms) still falls when the true one has landed,
   which isn't a jump.
Each --netsim value (HK_NETSIM, both instances: <latency ms>,<jitter ms>,<loss %>
per direction) is one run; "0" is a run without it. Default: 0 and 100,20,2.
--fps: HK_MAXFPS of both, or of the host then the client (no VSync: the frames keep
coming without the focus); the script's frames follow it (written for 60).
The client's own player, when the server moves it from its commands (docs/M16.md,
phases 2 and 3, HELLOCAP_INPUTS): each state from the server against what the client
predicted for that command (HK_SYNCLOG "syncpredict"): the gap, the corrections (a
gap of .01 unit or more: replayed and smoothed) per second, the snaps (> 3 units).
--vars: the host's server settings (HK_AC_VARS, e.g. sv_authmove=0); --legacy: the
client joins as a 0.7 build (HK_LEGACYMOVE=1), moving itself.
Local only (two windows), not in the CI. Linux needs a display (xvfb-run)."""
import argparse
import bisect
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(os.path.dirname(HERE), "data")
LANDGRIDSIZE = 512.0
SNAP = 0.25       # units more than the true motion in one frame: a visible jump
PORT = 37420

# the same walk for both, in frames at 60 fps, once both are surely in the game
START = 900
PATH = [
    # spawn: a client that joins has its 5 s respawn wait first, then fire spawns
    # (HK_PLAY is the spawn screen's, the host's)
    (-600, "log")] + [(f, s) for f in range(-560, -40, 60) for f, s in ((f, "down:righttrigger"), (f + 10, "up:righttrigger"))] + [
    (-20, "log"),
    (0, "axis:lefty:-1"),                  # walk
    (100, "axis:rightx:.5"), (160, "axis:rightx:0"),   # turn while walking
    (200, "axis:leftx:1"), (280, "axis:leftx:0"),      # and strafe
    (300, "down:lefttrigger"), (390, "up:lefttrigger"),  # jetpack up
    (400, "axis:lefty:0"),                 # let go, fall
    (500, "axis:lefty:-1"), (520, "axis:rightx:-.4"), (620, "axis:rightx:0"),
    (650, "press:a"),                      # jump while walking
    (700, "axis:lefty:1"), (800, "axis:lefty:0"),      # back, stop
    (850, "down:lefttrigger"), (920, "up:lefttrigger"),
    (930, "axis:lefty:-1"), (960, "axis:leftx:-1"), (1060, "axis:leftx:0"),
    (1100, "axis:lefty:0"),
    (1200, "log"),
]


def executable(path):
    if path.endswith(".app"):
        return os.path.join(path, "Contents", "MacOS", "Edifice")
    return path


def user_folder():
    """data/*.ini, CRLF as the game writes them; net.ini kept off the network"""
    folder = tempfile.mkdtemp(prefix="edifice-sync-")
    fix = {"lobby": "", "servervisibility": "lan", "infosendport": str(PORT), "datasendport": str(PORT + 1)}
    for name in os.listdir(DATA):
        if not name.endswith(".ini"):
            continue
        with open(os.path.join(DATA, name), "rb") as f:
            lines = f.read().decode("latin-1").replace("\r\n", "\n").split("\n")
        if name == "net.ini":
            lines = [k + "=" + fix[k] if k in fix else l for l, k in ((l, l.split("=")[0]) for l in lines)]
        with open(os.path.join(folder, name), "wb") as f:
            f.write("\r\n".join(lines).encode("latin-1"))
    return folder + os.sep


def script(fps=60):
    return ";".join("%d:%s" % (round((START + f) * fps / 60), s) for f, s in PATH)


def run(exe, netsim, fps, timeout, hostvars=None, legacy=False):
    """both logs (text); fps: of the host and the client; hostvars: its HK_AC_VARS
    (server settings, e.g. sv_authmove=1); legacy: the client joins as a 0.7 build
    (HK_LEGACYMOVE, no input commands)"""
    common = dict(SDL_AUDIODRIVER="dummy", HK_SYNCLOG="1", HK_PLAY="1", HK_SHOT_FRAME="99999999", HK_CONSOLE_LOG="1")
    if netsim != "0":
        common["HK_NETSIM"] = netsim
    procs, logs, dirs = [], [], []
    for role, rfps in zip(("host", "client"), fps):
        d = user_folder()
        dirs.append(d)
        env = dict(os.environ)
        for k in ("HK_AUTOSTART", "HK_JOIN", "HK_NETSIM", "HK_FIXEDCLOCK", "HK_AC_VARS", "HK_TEST_PORT"):
            env.pop(k, None)
        env.update(common, HK_USERDIR=d, HK_SHOT=os.path.join(d, "unused.ppm"), HK_MAXFPS=str(rfps),
                   HK_PADTEST=script(rfps))
        if role == "host":
            env.update(HK_AUTOSTART="1", HK_SEED="42")
            if hostvars:
                env["HK_AC_VARS"] = hostvars
        else:
            env.update(HK_JOIN="127.0.0.1", HK_SEED="43")
            if legacy:
                env["HK_LEGACYMOVE"] = "1"
        log = os.path.join(d, "log.txt")
        logs.append(log)
        with open(log, "wb") as f:
            procs.append(subprocess.Popen([exe], stdout=f, stderr=subprocess.STDOUT, env=env,
                                          stdin=subprocess.DEVNULL, cwd=d))
        if role == "host":
            time.sleep(2)   # listening before the client asks
    want = ["padtest: frame %d " % round((START + PATH[-1][0]) * f / 60) for f in fps]
    end = time.time() + timeout
    texts = ["", ""]
    try:
        while time.time() < end and all(p.poll() is None for p in procs):
            texts = [open(l, "rb").read().decode("utf-8", "replace") for l in logs]
            if all(w in t for w, t in zip(want, texts)):
                break
            time.sleep(0.5)
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
            p.wait()
    texts = [open(l, "rb").read().decode("utf-8", "replace") for l in logs]
    for d in dirs:
        shutil.rmtree(d, ignore_errors=True)
    return texts


LINE = re.compile(r"^synclog (\d+) (\d+) (\d+) (\d+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)$", re.M)


def samples(text):
    """local player number, {pnum: [(t s, state, (x,y,z))]}"""
    out, me = {}, None
    for m in LINE.finditer(text):
        t, me, pnum, state = int(m.group(1)) / 1e6, int(m.group(2)), int(m.group(3)), int(m.group(4))
        out.setdefault(pnum, []).append((t, state, (float(m.group(5)), float(m.group(6)), float(m.group(7)))))
    return me, out


def wrapdiff(a, b):
    """b - a, x and z across the edge of the repeating world"""
    d = [b[i] - a[i] for i in range(3)]
    for i in (0, 2):
        d[i] = (d[i] + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2
    return d


def norm(d):
    return math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])


class Path:
    """the true positions of a player, alive, read at any time"""
    def __init__(self, s):
        self.t = [x[0] for x in s if x[1] == 10]
        self.p = [x[2] for x in s if x[1] == 10]

    def at(self, t):
        i = bisect.bisect_left(self.t, t)
        if i <= 0 or i >= len(self.t):
            return None
        t0, t1 = self.t[i - 1], self.t[i]
        if t1 - t0 > 0.1:   # a hole (paused, not drawn)
            return None
        d = wrapdiff(self.p[i - 1], self.p[i])
        if norm(d) > 5:     # respawn
            return None
        f = (t - t0) / (t1 - t0) if t1 > t0 else 0
        return [self.p[i - 1][k] + d[k] * f for k in range(3)]


def measure(true, view):
    """stats of view (samples drawn by the other game) against true (the player's own)"""
    path = Path(true)
    view = [v for v in view if v[1] == 10]
    if len(view) < 60 or len(path.t) < 60:
        return None
    # from the scripted walk (its first horizontal motion): not the fall after the
    # spawn (which the other game draws rising, see docs/M16.md), nor a still start
    # (from where it last appeared: a client the server moves is put back where the
    # server respawned it, docs/M16.md phase 2)
    moving, base = None, 0
    for i in range(1, len(path.t)):
        if norm(wrapdiff(path.p[i - 1], path.p[i])) > 5:
            base = i
            continue
        d = wrapdiff(path.p[base], path.p[i])
        if math.hypot(d[0], d[2]) > 1:
            moving = path.t[i] - 0.5
            break
    if moving is None:
        return None
    view = [v for v in view if path.t[0] + 0.5 < v[0] < path.t[-1] - 0.5 and v[0] > moving]
    # from the first position received: until then the other game draws where the
    # player entered (0,0,0 or the corner of the world)
    for i, (t, _, p) in enumerate(view):
        q = path.at(t)
        if q is not None and norm(wrapdiff(q, p)) < 5:
            view = view[i:]
            break

    def errors(lag):
        e = []
        for t, _, p in view:
            q = path.at(t - lag)
            if q is not None:
                e.append(norm(wrapdiff(q, p)))
        return e

    e = errors(0)
    if len(e) < 60:
        return None
    best = min((sum(x) / len(x), lag) for lag in [l / 1000 for l in range(0, 501, 10)] for x in [errors(lag)] if x)
    def jumpsat(lag):
        """frames jumping more than SNAP units further than the true path, lag earlier,
        moved in the same time; and those excesses"""
        snaps, jumps = 0, []
        for a, b in zip(view, view[1:]):
            if b[0] - a[0] > 0.1:
                continue
            qa, qb = path.at(a[0] - lag), path.at(b[0] - lag)
            if qa is None or qb is None:
                continue
            j = norm([x - y for x, y in zip(wrapdiff(a[2], b[2]), wrapdiff(qa, qb))])
            jumps.append(j)
            if j > SNAP:
                snaps += 1
        return snaps, jumps

    snaps, jumps = jumpsat(0)
    # against the path as it was lag ago: a player drawn on time but late isn't jumping
    # when the true one stops a fall the drawn one is still in (docs/M16.md, phase 4)
    lagsnaps, _ = jumpsat(best[1])
    dur = view[-1][0] - view[0][0]
    # how fast the player went meanwhile, for scale
    i0, i1 = bisect.bisect_left(path.t, view[0][0]), bisect.bisect_left(path.t, view[-1][0])
    dist = sum(norm(wrapdiff(path.p[i], path.p[i + 1])) for i in range(i0, min(i1, len(path.p) - 1)))
    e.sort()
    jumps.sort()
    return dict(n=len(e), dur=dur, t0=view[0][0], t1=view[-1][0], mean=sum(e) / len(e), p95=e[int(len(e) * .95)], max=e[-1],
                lag=best[1] * 1000, lagerr=best[0], snaps=snaps, snaprate=snaps / dur if dur else 0,
                lagsnaprate=lagsnaps / dur if dur else 0,
                jump95=jumps[int(len(jumps) * .95)] if jumps else 0, speed=dist / dur if dur else 0)


PREDICT = re.compile(r"^syncpredict (\d+) (\d+) ([\d.]+) ([\d.]+) (\d) (\d+) ([\d.]+)$", re.M)
PREDICTSNAP = 3.0   # src/game/predict.h: a correction further than this isn't smoothed


def prediction(text, t0, t1):
    """the client's own player (docs/M16.md, phase 3): each state from the server
    against what it had predicted for that command, between t0 and t1 (s)"""
    errs, corr, snaps, shifts = [], 0, 0, []
    for m in PREDICT.finditer(text):
        t = int(m.group(1)) / 1e6
        if not t0 <= t <= t1:
            continue
        errs.append(float(m.group(3)))
        if m.group(5) == "1":
            corr += 1
            shift = float(m.group(7))
            if shift > PREDICTSNAP:
                snaps += 1
            else:
                shifts.append(shift)
    if not errs:
        return None
    errs.sort()
    shifts.sort()
    dur = t1 - t0
    return dict(n=len(errs), mean=sum(errs) / len(errs), p95=errs[int(len(errs) * .95)], max=errs[-1],
                corr=corr, corrrate=corr / dur if dur else 0, snaps=snaps,
                shift95=shifts[int(len(shifts) * .95)] if shifts else 0, shiftmax=shifts[-1] if shifts else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--netsim", nargs="*", default=["0", "100,20,2"])
    ap.add_argument("--fps", default="60")
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--keep", help="folder to keep both logs in")
    ap.add_argument("--vars", help="the host's server settings (HK_AC_VARS), e.g. sv_authmove=1")
    ap.add_argument("--legacy", action="store_true", help="the client joins as a 0.7 build (HK_LEGACYMOVE)")
    a = ap.parse_args()
    exe = os.path.abspath(executable(a.binary))
    fps = [int(f) for f in a.fps.split(",")]
    fps = (fps * 2)[:2]
    rows, preds, ok = [], [], True
    for ns in a.netsim:
        print("== HK_NETSIM=%s, host %d fps, client %d fps" % (ns, fps[0], fps[1]), flush=True)
        host, client = run(exe, ns, fps, a.timeout, a.vars, a.legacy)
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            tag = ns.replace(",", "_")
            open(os.path.join(a.keep, "host-%s.log" % tag), "w").write(host)
            open(os.path.join(a.keep, "client-%s.log" % tag), "w").write(client)
        hme, hs = samples(host)
        cme, cs = samples(client)
        if hme is None or cme is None or cme == hme:
            print("  no game: host %s, client %s" % (hme, cme))
            ok = False
            continue
        for what, true, view in (("client seen by host", cs.get(cme, []), hs.get(cme, [])),
                                 ("host seen by client", hs.get(hme, []), cs.get(hme, []))):
            r = measure(true, view)
            if not r:
                print("  %s: not enough samples (%d true, %d drawn)" % (what, len(true), len(view)))
                ok = False
                continue
            rows.append((ns, what, r))
            if what == "client seen by host":
                m = re.search(r"^netsyncmode (\d+) ", client, re.M)
                mode = "legacy"
                if m and int(m.group(1)) & 256:
                    mode = "snapshots" if int(m.group(1)) & 512 else "inputs"
                p = prediction(client, r["t0"], r["t1"])
                preds.append((ns, mode, p))
    print()
    print("%-10s %-20s %6s %6s %6s %6s %6s %7s %6s %7s %6s %6s" % ("netsim", "", "mean", "p95", "max", "lag", "@lag",
                                                                 "snaps/s", "jmp95", "@lag/s", "u/s", "secs"))
    for ns, what, r in rows:
        print("%-10s %-20s %6.2f %6.2f %6.2f %4.0fms %6.2f %7.2f %6.2f %7.2f %6.1f %6.1f" % (
            ns, what, r["mean"], r["p95"], r["max"], r["lag"], r["lagerr"], r["snaprate"], r["jump95"],
            r["lagsnaprate"], r["speed"], r["dur"]))
    print("(units; lag: delay that best explains the drawn path, @lag: mean error left once shifted;"
          " snaps: frames jumping > %.2f units more than the true motion; jmp95: p95 of that excess;"
          " @lag/s: snaps against the true motion lag earlier; u/s: the player's mean speed)" % SNAP)
    print()
    print("%-10s %-14s %6s %8s %8s %8s %7s %8s %6s %7s %7s" % ("netsim", "client's own", "states", "err", "err p95",
                                                           "err max", "corr", "corr/s", "snaps", "shft95", "shftmax"))
    for ns, mode, p in preds:
        if not p:
            print("%-10s %-14s (moves itself: nothing to predict)" % (ns, mode))
            continue
        print("%-10s %-14s %6d %8.4f %8.4f %8.4f %7d %8.2f %6d %7.3f %7.3f" % (
            ns, mode, p["n"], p["mean"], p["p95"], p["max"], p["corr"], p["corrrate"], p["snaps"], p["shift95"],
            p["shiftmax"]))
    print("(phase 3: each state of the client's player from the server against its prediction for that command;"
          " corr: corrected (gap >= .01 unit), replayed and smoothed; snaps: moved > %.0f units at once;"
          " shft: how far a correction moved the player)" % PREDICTSNAP)
    return 0 if ok and rows else 1


if __name__ == "__main__":
    sys.exit(main())
