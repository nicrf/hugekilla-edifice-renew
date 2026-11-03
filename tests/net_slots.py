"""Stable player slots (roadmap M11 phase 3b, docs/M11.md), protocol 6:
    HK_ANTICHEAT_LOG=1 HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_slots.py server.log
Four clients of this build (HELLOCAP_SERVERHITS, STABLESLOTS; tests/netclient.py:
input commands, the server moves them, HK_TESTPLACE puts them where the test wants)
join a local server: slots 1 to 4.
 - The one in slot 2 leaves: the others get DELETEPLAYER with PLAYERLEFT_KEEPSLOTS,
   keep their numbers (each is still in the snapshots under it) and their state
   (the life the server counts for the victim goes on down from where it was, its
   shooter still credited under its number).
 - A new client takes the free slot 2, and is told about everyone under their
   numbers (PLAYERNAMES: names, teams).
 - A client without HELLOCAP_STABLESLOTS joins: slot 5. Then slot 2 leaves again:
   with it in the game the server renumbers as before (DELETEPLAYER without the
   flag); everyone after slot 2 moves down one, the server's state with them (the
   victim's life, its shooter's number), and the snapshots under the new numbers.
The server log (its stderr) must show no anti-cheat event."""
import struct, sys
from netclient import *

LOG = sys.argv[1] if len(sys.argv) > 1 else None
trailer, caps, info = server_info()
game = Game()
check = game.check
check(caps & HELLOCAP_STABLESLOTS, "INFO says the server keeps slots")

for name in ("Alice", "Bob", "Carol", "Dan"):
    c = Client(name)
    if not c.join(trailer, game.clients):
        print("FAIL: %s could not join" % name); sys.exit(1)
    game.add(c)
    print("%s joined as slot %d" % (name, c.slot))
alice, bob, carol, dan = game.clients
check([c.slot for c in game.clients] == [1, 2, 3, 4], "slots 1 to 4")
game.pump(1.0)
teams = {}
for cl in game.clients:
    teams.update(cl.teams)
print("teams:", teams)

# Carol (3) is the victim, a player of another team after slot 2 shoots her (the
# teams alternate: Dan), Alice watches, Bob (2) leaves
victim = carol
shooter = next((c for c in (dan, alice) if teams.get(c.slot, 0) != teams.get(victim.slot, 0) or
                teams.get(c.slot, 0) == 0), None)
if not shooter:
    print("FAIL: nobody to shoot the victim"); sys.exit(1)
eye = alice if shooter is not alice else dan


def playing():
    return [c for c in game.clients if hasattr(c, "client")]


def place():
    """victim, shooter 2 units on x from it, the others away, on the ground"""
    victim.place(150, 120, 150)
    shooter.place(148, 120, 150)
    for k, c in enumerate(playing()):
        if c not in (victim, shooter):
            c.place(320 + 20 * k, 120, 320)
    game.settle(playing(), 6)


def shoot(n):
    """n aimed shots at the victim (from where the server has the shooter, at where the
    shooter sees it); its SERVERHITs (by, life)"""
    place()
    v = shooter.others().get(victim.slot)
    if not v or not shooter.own:
        return None
    mark = victim.mark()
    for k in range(n):
        shooter.fire(aim(shooter.own[0:3], v))
        game.pump(0.15)
    game.pump(0.5)
    return victim.simples(mark, SERVERHIT)


got = shoot(3)
print("before the leave, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and 0 < got[-1][1] < 100,
      "the victim is hurt by %d, alive" % shooter.slot)
life1 = got[-1][1] if got else 100

# ---- slot 2 leaves: everyone keeps its number ----
marks = {c.name: c.mark() for c in game.clients}
bob.leave()
game.pump(1.0)
for c in (alice, carol, dan):
    keeps = [(a, b) for a, b in c.simples(marks[c.name], DELETEPLAYER) if a == 2]
    check(len(keeps) == 1 and keeps[0][1] & PLAYERLEFT_KEEPSLOTS, "%s: DELETEPLAYER 2 keeps the slots" % c.name.decode())
check([c.slot for c in (alice, carol, dan)] == [1, 3, 4], "the others keep slots 1, 3, 4")
carol.place(222, 120, 111)
dan.place(333, 120, 222)
game.pump(0.6)
seen = alice.others()
check(seen.get(3) and wrapdist(seen[3], (222, seen[3][1], 111)) < 1 and seen.get(4) and
      wrapdist(seen[4], (333, seen[4][1], 222)) < 1,
      "they are in the snapshots under 3 and 4 (%s, %s)" % (seen.get(3), seen.get(4)))
check(2 not in seen, "and nobody under the free slot 2")
got = shoot(2)
print("after the leave, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and got[0][1] < life1,
      "the server's state stayed with them: the victim's life goes on down from %d, hit by %d" % (life1, shooter.slot))
life2 = got[-1][1] if got else life1

# ---- a new client takes the free slot ----
mark = alice.mark()
erin = Client("Erin")
if not erin.join(trailer, game.clients):
    print("FAIL: Erin could not join"); sys.exit(1)
game.add(erin)
game.pump(0.5)
check(erin.slot == 2, "the new client takes the free slot 2 (got %d)" % erin.slot)
listed = {n[0]: (n[4], n[3]) for m in erin.got if m[0] == PLAYERNAMES for n in player_names(m)}
print("Erin's PLAYERNAMES:", listed)
check(listed.get(1, (0,))[0] == b"Alice" and listed.get(3, (0,))[0] == b"Carol" and listed.get(4, (0,))[0] == b"Dan",
      "it is told about the others under their numbers")
check(all(listed.get(c.slot, (0, -1))[1] == teams.get(c.slot, 0) for c in (alice, carol, dan)), "with their teams")
joined = [n for m in alice.since(mark) if m[0] == PLAYERNAMES for n in player_names(m)]
check(any(n[0] == 2 and n[4] == b"Erin" for n in joined), "the others are told Erin is 2")

# ---- a client that can't keep the slots: the server renumbers when slot 2 leaves ----
frank = Client("Frank", HELLOCAP_SERVERHITS | HELLOCAP_SMOKE)
if not frank.join(trailer, game.clients):
    print("FAIL: Frank could not join"); sys.exit(1)
game.add(frank)
check(frank.slot == 5, "the client without HELLOCAP_STABLESLOTS joins as slot 5 (got %d)" % frank.slot)
frank.place(400, 120, 400)
game.pump(0.8)
check(alice.others().get(5) and wrapdist(alice.others()[5], (400, alice.others()[5][1], 400)) < 1,
      "it is in the snapshots, where the server put it")
oldshooter = shooter.slot
marks = {c.name: c.mark() for c in playing()}
erin.leave()
game.pump(1.0)
for c in playing():
    dels = [(a, b) for a, b in c.simples(marks[c.name], DELETEPLAYER) if a == 2]
    check(len(dels) == 1 and not dels[0][1] & PLAYERLEFT_KEEPSLOTS,
          "%s: DELETEPLAYER 2 renumbers (a client that can't keep them is in)" % c.name.decode())
check([c.slot for c in playing()] == [1, 2, 3, 4], "Alice 1, Carol 2, Dan 3, Frank 4 (%s)" % [c.slot for c in playing()])
carol.place(222, 120, 111)
dan.place(333, 120, 222)
frank.place(444, 120, 333)
game.pump(1.0)
seen = alice.others()
spots = {2: (222, 111), 3: (333, 222), 4: (444, 333)}
check(all(seen.get(k) and wrapdist(seen[k], (x, seen[k][1], z)) < 1 for k, (x, z) in spots.items()),
      "in the snapshots under the new numbers (%s)" % ({k: seen.get(k) for k in spots},))
fs = frank.others().get(2)
check(fs and wrapdist(fs, (222, fs[1], 111)) < 1, "it sees them there too")
got = shoot(2)
print("after the renumbering, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and got[0][1] < life2,
      "the victim's life moved with it (goes on down from %d), its shooter is now %d (was %d)" % (life2, shooter.slot, oldshooter))
game.finish(LOG)
