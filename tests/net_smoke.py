"""Smoke grenades (src/game/smoke.h) against a local server:
    HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice binary> &
    python3 tests/net_smoke.py
Clients of protocol 6 (tests/netclient.py: input commands, the server moves them).
 - INFO announces HELLOCAP_SMOKE.
 - A client with HELLOCAP_SMOKE joins alone: the server tells it it takes smoke
   grenades (SMOKEMODE 1). It throws one (THROWSMOKE + FireTick, grenade in hand):
   it gets the server object (type SMOKECLOUD) flying, then landed with its age.
 - A client without HELLOCAP_SMOKE joins while the cloud lives: its snapshot has an
   empty slot instead, it plays on (each sees the other in the snapshots), and the
   first is told smoke grenades are off (SMOKEMODE 0): a throw now makes nothing.
 - A second client with them joining then gets the cloud in its snapshot, landed,
   with an age that fits; at the end of its life the server removes it (type 0) and
   the client without them never got a smoke object at all.
HK_TEST_PORT: the server's info port if not 27020 (net.ini of its HK_USERDIR)."""
import struct, sys, time
from netclient import *

SMOKECLOUD, LIFETIME = 5, 25.0
MAXSERVEROBJS, OBJSIZE = 100, 43


def sobj(buf, off):
    """a server object: type, pos, dir, vel, team, attached"""
    t = struct.unpack_from("<b", buf, off)[0]
    v = struct.unpack_from("<9f", buf, off + 1)
    team, att = struct.unpack_from("<bb", buf, off + 37)
    return dict(type=t, pos=v[0:3], dir=v[3:6], vel=v[6:9], team=team, attached=att)


def smokemodes(c):
    return [a for a, b in c.simples(0, SMOKEMODE)]


def updates(c, mark=0):
    """(time, slot, object) of every UPDATESERVEROBJECT since mark"""
    return [(c.at[i], struct.unpack_from("<i", m, 1)[0], sobj(m, 5)) for i, m in enumerate(c.got)
            if i >= mark and m[0] == UPDATESERVEROBJECT and len(m) >= 5 + OBJSIZE]


def objsnapshot(c):
    for m in c.got:
        if m[0] == SERVEROBJECTSNAPSHOT and len(m) >= 1 + MAXSERVEROBJS * OBJSIZE:
            return [sobj(m, 1 + i * OBJSIZE) for i in range(MAXSERVEROBJS)]
    return None


trailer, caps, info = server_info()
game = Game()
check = game.check
check(caps & HELLOCAP_SMOKE, "INFO announces smoke grenades (caps %d)" % caps)

# ---- a new client alone: smoke grenades on, it throws one ----
new = Client("Smoky", HELLOCAP_SMOKE)
if not new.join(trailer):
    print("FAIL: the new client could not join"); sys.exit(1)
game.add(new)
print("new client joined as slot %d" % new.slot)
new.weapon = 4
new.place(600, 100, 600)
game.settle([new], 6)
check(smokemodes(new)[-1:] == [1], "the server takes smoke grenades (SMOKEMODE %s)" % smokemodes(new))
x, y, z = new.own[0:3]
mark = new.mark()
thrown = time.time()
new.fire((0.7, 0.2, 0.7), kind=THROWSMOKE, objid=5)
game.pump(4.0)
ups = [(t, s, o) for t, s, o in updates(new, mark) if o["type"] == SMOKECLOUD]
flying = [u for u in ups if u[2]["dir"][0] < 0.5]
landed = [u for u in ups if u[2]["dir"][0] > 0.5]
check(flying, "it got the grenade flying (%d updates)" % len(flying))
check(landed, "and landed as a cloud")
slot = landed[0][1] if landed else -1
if landed:
    o = landed[0][2]
    print("  landed at %.1f,%.1f,%.1f, age %.2f s, lifetime %.0f s" % (*o["pos"], o["dir"][1], o["dir"][2]))
    check(0 <= o["dir"][1] < 1 and o["dir"][2] == LIFETIME and o["vel"] == (0, 0, 0),
          "landed with age %.2f, lifetime %.0f, at rest" % (o["dir"][1], o["dir"][2]))
    check(o["attached"] == -1, "nobody carries it")

# ---- a client without smoke grenades joins while it lives ----
old = Client("Oldtimer", 0)
if not old.join(trailer, game.clients):
    print("FAIL: the client without smoke grenades could not join"); sys.exit(1)
game.add(old)
old.place(x + 3, y + 2, z + 3)
game.pump(2.0)
snap = objsnapshot(old)
check(snap is not None, "the client without them got its snapshot")
if snap is not None:
    check(all(o["type"] in (0, 1, 2, 3) for o in snap), "no smoke object in it (types %s)" % sorted({o["type"] for o in snap}))
    if slot >= 0:
        check(snap[slot]["type"] == 0, "slot %d is empty for it" % slot)
check(smokemodes(new)[-1:] == [0], "the new client is told smoke grenades are off (SMOKEMODE %s)" % smokemodes(new))
check(new.slot in old.others() and old.slot in new.others(), "both see each other (SNAPSHOT)")
mark = new.mark()
new.fire((0.7, 0.2, 0.7), kind=THROWSMOKE, objid=6)
game.pump(2.5)
check(not [u for u in updates(new, mark) if u[2]["type"] == SMOKECLOUD and u[1] != slot],
      "a throw now makes no new cloud")

# ---- a second new client joins: the cloud in its snapshot ----
late = Client("Latecomer", HELLOCAP_SMOKE)
if not late.join(trailer, game.clients):
    print("FAIL: the second new client could not join"); sys.exit(1)
game.add(late)
late.place(x - 3, y + 2, z - 3)
game.pump(1.0)
snap = objsnapshot(late)
if snap is not None and slot >= 0:
    o = snap[slot]
    age = time.time() - (landed[0][0] if landed else thrown)
    check(o["type"] == SMOKECLOUD and o["dir"][0] > 0.5, "the second new client got the cloud in its snapshot")
    check(abs(o["dir"][1] - age) < 2.5, "with its age (%.1f s, about %.1f)" % (o["dir"][1], age))
else:
    check(False, "the second new client got a snapshot")

# ---- its end ----
left = LIFETIME - (time.time() - (landed[0][0] if landed else thrown))
print("waiting %.0f s for the cloud to end" % max(left, 0))
game.pump(max(left, 0) + 3)
ends = [u for u in updates(new, mark) if u[1] == slot and u[2]["type"] == 0]
check(ends, "the server removed it after its lifetime")
check(not [u for u in updates(old) if u[2]["type"] == SMOKECLOUD], "the client without them never got a smoke object")
check(new.slot in old.others() and time.time() - old.at[-1] < 2, "it still plays (messages flowing)")
game.finish()
