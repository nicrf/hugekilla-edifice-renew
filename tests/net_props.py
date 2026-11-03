"""Rocks and trees that block (docs/PROPS.md), against a local server:
    HK_PROPS_LOG=1 HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=1" HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice> 2>server.log &
    python3 tests/net_props.py server.log <Edifice> on
(and with props=0 and "off"). HK_TEST_PORT: the server's info port (27020).
Clients of protocol 6 (tests/netclient.py: input commands, the server moves them,
HK_TESTPLACE puts them where the test wants). With props on:
 - INFO announces HELLOCAP_PROPS; a client without it (a build without them) is
   refused with VERSIONMISMATCH and the text, and gets no slot;
 - clients with it join and get PROPSMODE 1 with the server's props hash (its log);
 - a game client (HK_JOIN) builds the same props from the land: same count, same hash;
 - a bullet the server simulates stops on a trunk: no SERVERHIT for a victim behind
   the tree, while the same shot beside the tree hits it.
With props off: no HELLOCAP_PROPS in INFO, a client without them joins, one with them
gets PROPSMODE 0."""
import math, os, re, shutil, struct, subprocess, sys, tempfile, time
from netclient import *

PORT = SERVER[1]
LOG = sys.argv[1]
GAME = sys.argv[2] if len(sys.argv) > 2 else None
MODE = sys.argv[3] if len(sys.argv) > 3 else "on"
NEW = HELLOCAP_SERVERHITS | HELLOCAP_PROPS


def server_props():
    """(count, hash, [(kind, x, y, z, r, h)]) of the server's last land, from its log"""
    for _ in range(50):
        log = open(LOG, errors="replace").read()
        heads = [m for m in re.finditer(r"^props: (\d+) hash ([0-9a-f]{8})$", log, re.M)]
        if heads:
            last = heads[-1]
            lines = re.findall(r"^prop \d+ (\d) (\S+) (\S+) (\S+) (\S+) (\S+)$", log[last.end():], re.M)
            props = [(int(k), *map(float, rest)) for k, *rest in lines][:int(last.group(1))]
            return int(last.group(1)), int(last.group(2), 16), props
        time.sleep(0.2)
    return None


trailer, caps, info = server_info()
proto = struct.unpack_from("<H", trailer, 4)[0]
game = Game()
check = game.check
clients = game.clients
sp = server_props()
if not sp:
    print("FAIL: no 'props:' line in the server log (HK_PROPS_LOG=1)"); sys.exit(1)
count, phash, props = sp
print("server: %d rocks and trees, hash %08x, INFO caps %d" % (count, phash, caps))

if MODE == "off":
    check(not caps & HELLOCAP_PROPS, "INFO does not announce rocks and trees")
    old = Client("Old", HELLOCAP_SERVERHITS)
    check(old.join(trailer), "a client without them joins")
    game.add(old)
    new = Client("New", NEW)
    check(new.join(trailer, clients), "a client with them joins")
    game.add(new)
    game.pump(1.0)
    check((0, phash) in new.simples(0, PROPSMODE) or any(a == 0 for a, b in new.simples(0, PROPSMODE)),
          "and is told there are none (PROPSMODE 0)")
    check(not old.simples(0, PROPSMODE), "the client without them never gets PROPSMODE")
    game.finish()

check(caps & HELLOCAP_PROPS, "INFO announces rocks and trees (HELLOCAP_PROPS)")
check(count > 50 and len(props) == count, "the server has %d of them, all logged" % count)

# ---- a build without them is refused, with the reason ----
old = Client("Old", HELLOCAP_SERVERHITS)
check(not old.join(trailer), "a client without HELLOCAP_PROPS gets no slot")
refused = old.simples(0, VERSIONMISMATCH)
text = b"".join(m for m in old.got if m[0] == CHATMESSAGE)
check(refused and refused[0][0] == proto, "it is refused (VERSIONMISMATCH, protocol %d)" % proto)
check(b"rocks and trees" in text, "with the text %r" % text[5:].decode(errors="replace"))

# ---- new clients: PROPSMODE with the server's hash ----
for n in ("Alice", "Bob", "Carol"):
    c = Client(n, NEW)
    if not c.join(trailer, clients):
        print("FAIL: %s could not join" % n); sys.exit(1)
    game.add(c)
    print("%s joined as slot %d" % (n, c.slot))
game.pump(1.0)
for c in clients:
    modes = c.simples(0, PROPSMODE)
    check(modes and modes[-1] == (1, phash), "%s: PROPSMODE 1, hash %s" % (
        c.name.decode(), "%08x" % modes[-1][1] if modes else "none"))

# ---- a game client builds the same ones ----
if GAME:
    d = tempfile.mkdtemp(prefix="edifice-props-")
    for f in ("control", "land", "net", "object", "skies", "sv"):
        shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", f + ".ini"), d)
    lines = open(os.path.join(d, "net.ini"), "rb").read().decode().split("\r\n")
    fix = {"infosendport": str(PORT), "datasendport": str(PORT + 1), "lobby": "", "servervisibility": "lan"}
    lines = [k + "=" + fix[k] if k in fix else l for l, k in ((l, l.split("=")[0]) for l in lines)]
    open(os.path.join(d, "net.ini"), "wb").write("\r\n".join(lines).encode())
    clog = os.path.join(os.path.dirname(os.path.abspath(LOG)), "net_props-client.log")
    env = dict(os.environ, HK_USERDIR=d + "/", HK_JOIN="127.0.0.1", HK_PROPS_LOG="1")
    env.pop("HK_AUTOSTART", None)
    env.pop("HK_AC_VARS", None)
    proc = subprocess.Popen([GAME], env=env, stdout=open(clog, "wb"), stderr=subprocess.STDOUT)
    same = None
    end = time.time() + 60
    while time.time() < end and same is None:
        game.pump(0.5)
        c = open(clog, errors="replace").read()
        m = re.search(r"rocks and trees: (\d+), same as the server's \(([0-9a-f]{8})\)", c)
        if m:
            same = (int(m.group(1)), int(m.group(2), 16))
        elif "rocks and trees differ" in c:
            same = False
    proc.kill()
    proc.wait()
    shutil.rmtree(d, ignore_errors=True)
    check(same and same == (count, phash), "a game client (HK_JOIN) built the same %s rocks and trees" % (
        same[0] if same else "?"))
    clients_props = re.findall(r"^prop \d+ .*$", open(clog, errors="replace").read(), re.M)
    serverlines = re.findall(r"^prop \d+ .*$", open(LOG, errors="replace").read(), re.M)[-count:]
    check(clients_props[-count:] == serverlines, "the same %d positions, sizes, heights" % count)
else:
    print("no game binary given: the game client's props not checked")

# ---- a bullet stops on a trunk ----
teams = {}
for cl in clients:
    teams.update(cl.teams)
pair = None
for v in clients:
    for s in clients:
        tv, ts = teams.get(v.slot, 0), teams.get(s.slot, 0)
        if v is not s and (tv != ts or tv == 0):
            pair = (v, s)
            break
    if pair:
        break
if not pair:
    print("FAIL: no two players on different teams"); sys.exit(1)
victim, shooter = pair
eye = next(c for c in clients if c is not victim and c is not shooter)
print("victim %d, shooter %d, watcher %d" % (victim.slot, shooter.slot, eye.slot))


def place(vp, sp):
    """victim at vp, shooter at sp (x, z), dropped there; where the shooter sees the
    victim, and where the server has the shooter"""
    victim.place(vp[0], 120, vp[1])
    shooter.place(sp[0], 120, sp[1])
    eye.place(vp[0] + 60, 120, vp[1] + 60)
    game.settle(clients, 6)
    return shooter.others().get(victim.slot), shooter.own and shooter.own[0:3]


def volley(v, g, n=12):
    """n shots from the shooter's place at the victim's body: SERVERHITs got"""
    mark = victim.mark()
    d = aim(g, v)
    game.pump(0.15 * n, each=lambda k: shooter.fire(d) if k % 9 == 0 else None)
    game.pump(0.6)
    return len(victim.simples(mark, SERVERHIT))


def lonely(p):
    return all(math.hypot(q[1] - p[1], q[3] - p[3]) > 12 for q in props if q is not p)


def wrapz(a, b):
    return abs((a - b + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2)


trees = [p for p in props if p[0] in (0, 1) and lonely(p)]
print("%d lone trees to try" % len(trees))
done = False
for p in trees[:6]:
    _, tx, ty, tz, tr, th = p
    # behind the trunk 3 units on x, the shooter 6 units in front
    v, g = place((tx + 3, tz), (tx - 6, tz))
    if not v or not g or wrapz(v[2], tz) > 0.5 or wrapz(g[2], tz) > 0.5:
        continue
    shooter.fire((0, 1, 0), reload=1)   # a full magazine
    game.pump(2.6)
    blocked = volley(v, g)
    # the same shots 4 units beside the tree: the line of sight was good
    v2, g2 = place((tx + 3, tz + 4), (tx - 6, tz + 4))
    if not v2 or not g2:
        continue
    shooter.fire((0, 1, 0), reload=1)
    game.pump(2.6)
    beside = volley(v2, g2, 4)
    print("tree at %.1f %.1f (radius %.2f, %.1f high): %d SERVERHIT through it, %d beside it" % (
        tx, tz, tr, th, blocked, beside))
    if beside:
        check(blocked == 0, "shots through a trunk do no damage (%d SERVERHIT)" % blocked)
        done = True
        break
check(done, "found a tree with a clear shot beside it")
game.finish()
