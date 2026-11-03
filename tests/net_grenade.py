"""Shots at a tick, grenades the server owns, timestamped events (docs/M16.md, phase 5):
    HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=0" HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_grenade.py server.log
Two clients join a local server (protocol 6, tests/netclient.py), with smoke grenades.
 - The thrower throws a smoke grenade (THROWSMOKE + FireTick, its number 88): it
   gets the server's object (UPDATESERVEROBJECT, SMOKECLOUD), then TE_SMOKE naming
   that slot and its number (its predicted one makes way); the other gets the
   server's object, not the TE_SMOKE.
 - It fires a bullet (FIREGUN + FireTick): the other gets FIREGUN with the server's
   tick after it (it waits until it draws that tick).
 - A shot without its tick (FIREGUN alone, protocol 5) goes nowhere.
 - It throws a frag grenade at its feet (FIREGUN + FireTick, number 77): both get
   TE_GRENADE (the thrower too: its predicted copy takes it over), thrown from where
   the server had the thrower after the command it named, and no FIREGUN for it. It
   goes off by the thrower, a fuse later (TE_EXPLODE); the thrower dies (TE_KILLED at
   that tick) and the other hears its pain then (TE_PAIN); no KILLED nor PAIN.
The server log (its stderr) must show no anti-cheat event."""
import struct, sys
from netclient import *

FUSE = 1700   # sv.ini grenadeexplodetime, ms
LOG = sys.argv[1] if len(sys.argv) > 1 else None

trailer, caps, info = server_info()
game = Game()
check = game.check
thrower, watcher = Client("Thrower"), Client("Watcher")
for cl in (thrower, watcher):
    if not cl.join(trailer, game.clients):
        print("FAIL: %s could not join" % cl.name.decode()); sys.exit(1)
    game.add(cl)
print("Thrower in slot %d, Watcher in slot %d" % (thrower.slot, watcher.slot))
thrower.weapon = 4
# alive (the server puts them somewhere), down to the ground
check(game.settle([thrower, watcher], 8), "both stand on the ground (%s)" % (
    thrower.own and [round(v, 3) for v in thrower.own],))
if not thrower.own:
    sys.exit(1)
here = thrower.own[0:3]

# a smoke grenade
mark = thrower.mark()
wmark = watcher.mark()
thrower.fire((1, .5, 0), kind=THROWSMOKE, objid=88)
game.pump(1.0)
seq = [m for m in thrower.since(mark) if m[0] in (UPDATESERVEROBJECT, TICKEVENT)]
smokeat = next((i for i, m in enumerate(seq) if m[0] == UPDATESERVEROBJECT and m[5] == 5), None)
slot = struct.unpack_from("<i", seq[smokeat], 1)[0] if smokeat is not None else None
te = [(i, tickevent(m)) for i, m in enumerate(seq) if m[0] == TICKEVENT and m[5] == TE_SMOKE]
check(slot is not None and te and te[0][1]["objid"] == 88 and te[0][1]["slot"] == slot and te[0][1]["player"] == thrower.slot,
      "the thrower is told the server's smoke grenade (slot %s) takes the place of its number 88 (%s)" % (
          slot, te and te[0][1]))
check(te and smokeat is not None and te[0][0] > smokeat, "after the server's object itself")
objs = [m for m in watcher.since(wmark) if m[0] == UPDATESERVEROBJECT and m[5] == 5]
check(objs and struct.unpack_from("<i", objs[0], 1)[0] == slot, "the other sees the server's smoke grenade")
check(not watcher.events(wmark, TE_SMOKE), "the other isn't told about the number")

# a bullet
thrower.weapon = 3
game.pump(0.5)
wmark = watcher.mark()
tick0 = thrower.newest
thrower.fire((1, 0, 0))
game.pump(0.6)
wf = [m for m in watcher.since(wmark) if m[0] == FIREGUN and m[1] == thrower.slot]
check(wf and len(wf[0]) == 31 and abs(((struct.unpack_from("<I", wf[0], 27)[0] - tick0 + (1 << 31)) & 0xFFFFFFFF) - (1 << 31)) < 30,
      "the other gets the shot with the server's tick (%s bytes, tick %s near %s)" % (
          wf and len(wf[0]), wf and struct.unpack_from("<I", wf[0], 27)[0], tick0))
check(not watcher.events(wmark, TE_GRENADE), "a bullet is no grenade")
# protocol 5's FIREGUN, without its tick: nothing
game.pump(0.5)
wmark = watcher.mark()
thrower.send(thrower.shot((1, 0, 0))[:27])
game.pump(0.6)
check(not [m for m in watcher.since(wmark) if m[0] == FIREGUN], "a shot without its tick goes nowhere")

# a frag grenade, at its feet: it goes off by the thrower and kills it
thrower.weapon = 4
game.pump(0.6)
mark, wmark = thrower.mark(), watcher.mark()
here = thrower.own[0:3]
thrower.fire((0, -1, 0), objid=77)
game.pump(FUSE / 1000 + 1.5)
for cl, m0 in ((thrower, mark), (watcher, wmark)):
    g = cl.events(m0, TE_GRENADE)
    check(g and g[0]["objid"] == 77 and g[0]["player"] == thrower.slot and wrapdist(g[0]["pos"], here) < .5 and
          g[0]["vel"][1] < 0 and abs(g[0]["ms"] - FUSE) < 100,
          "%s gets the server's grenade, number 77, thrown from where the thrower was (%s)" % (
              cl.name.decode(), g and (g[0]["objid"], [round(v, 2) for v in g[0]["pos"]], g[0]["ms"])))
    check(not [m for m in cl.since(m0) if m[0] == FIREGUN and m[1] == thrower.slot], "and no FIREGUN for it")
    x = cl.events(m0, TE_EXPLODE)
    check(g and x and x[0]["objid"] == 77 and abs(((x[0]["tick"] - g[0]["tick"]) & 0xFFFFFFFF) - FUSE * 60 / 1000) < 6 and
          wrapdist(x[0]["pos"], here) < 3,
          "%s: it goes off a fuse later, by the thrower (TE_EXPLODE %s ticks later, at %s)" % (
              cl.name.decode(), x and g and (x[0]["tick"] - g[0]["tick"]) & 0xFFFFFFFF,
              x and [round(v, 2) for v in x[0]["pos"]]))
x = watcher.events(wmark, TE_EXPLODE)
k = watcher.events(wmark, TE_KILLED)
check(k and k[0]["killed"] == thrower.slot and k[0]["killer"] == thrower.slot and x and k[0]["tick"] == x[0]["tick"],
      "the other is told the thrower died, at the tick it went off (%s)" % (k and k[0]))
kt = thrower.events(mark, TE_KILLED)
check(kt and kt[0]["killed"] == thrower.slot, "the thrower is told it died")
p = watcher.events(wmark, TE_PAIN)
check(p and p[0]["player"] == thrower.slot and x and p[0]["tick"] == x[0]["tick"], "and hears its pain at that tick (TE_PAIN)")
check(not watcher.simples(wmark, KILLED) and not watcher.simples(wmark, PAIN), "no KILLED nor PAIN")
game.finish(LOG)
