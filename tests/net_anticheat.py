"""Anti-cheat checks of the server (roadmap M11 phases 1 and 3a, docs/M11.md), protocol 6:
    HK_ANTICHEAT_LOG=1 HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_anticheat.py server.log
Clients of protocol 6 (tests/netclient.py: input commands, the server moves them,
HK_TESTPLACE puts them where the test wants). Three join without
HELLOCAP_SERVERHITS (they would count their own damage); a fourth announces it.
 - A victim of the other team is shot at point blank and never sends KILLED:
   the server kills it itself (TE_KILLED to everyone, the victim too), keeps it
   dead (state 11 in the snapshots, its shots dropped) whatever its commands say.
 - A flood of KILLED / KILLEDPOSITION from one client: one death counted, not
   credited to the player it blames (the server never saw it hit).
 - Shots faster than the gat allows, or more than its magazine: not relayed.
 - Blocks far from their builder: refused; near it: relayed.
 - Info requests from one IP are rate limited.
 - Phase 3a, against the new client: the server tells it it decides hits
   (HITMODE); a shot that says it comes from next to it leaves from where the server
   has its shooter (60 units away); shots that miss it (aimed away, or beside it)
   and its own KILLED blaming someone do nothing; a real shot from the shooter's
   place hurts it (SERVERHIT, TE_PAIN for the others) until the server kills it.
The server log (its stderr) must show each event."""
import socket, struct, sys, time
from netclient import *

LOG = sys.argv[1] if len(sys.argv) > 1 else None
CREDITS, BLOCKSIZE = 24, 2
OLD = HELLOCAP_STABLESLOTS | HELLOCAP_SMOKE   # counts its own damage (no HELLOCAP_SERVERHITS)


def block(x, y, z, delete=0):
    return struct.pack("<bI", BLOCKS, 1) + struct.pack("<bbbbbbbbb", x, y, z, 1, 0, 0, 0, 0, delete)


def fires(cl, slot, mark):
    """FIREGUNs relayed to cl from player slot since mark"""
    return [m for m in cl.since(mark) if m[0] == FIREGUN and len(m) >= 27 and m[1] == slot]


def kills(cl, mark, killed=None, killer=None):
    return [e for e in cl.events(mark, TE_KILLED) if (killed is None or e["killed"] == killed) and
            (killer is None or e["killer"] == killer)]


def credits(cl):
    got = [b for a, b in cl.simples(0, CREDITS) if a == 0]
    return got[-1] if got else 0


trailer, caps, info = server_info()
game = Game()
check = game.check
clients = game.clients
for n in ("Alice", "Bob", "Carol"):
    c = Client(n, OLD)
    if not c.join(trailer, clients):
        print("FAIL: %s could not join" % n); sys.exit(1)
    game.add(c)
    print("%s joined as slot %d" % (n, c.slot))
game.pump(1.0)
teams = {}
for cl in clients:
    teams.update(cl.teams)
print("teams:", teams, " credits:", [credits(cl) for cl in clients])

# a victim and a shooter on different teams (free for all: team 0, anyone)
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
third = next(c for c in clients if c is not victim and c is not shooter)
print("victim %d (team %d), shooter %d (team %d), watcher %d" % (
    victim.slot, teams.get(victim.slot, 0), shooter.slot, teams.get(shooter.slot, 0), third.slot))
shooter.weapon = 0   # the gat: 100 ms, 50 rounds

# ---- the victim never sends KILLED: the server kills it ----
victim.place(100, 120, 100)
shooter.place(101, 120, 100)
third.place(300, 120, 300)
game.settle(clients, 6)
start = {cl.name: cl.mark() for cl in clients}
killed, shots = False, 0
for k in range(60):
    v = shooter.others().get(victim.slot)
    if not v or not shooter.own:
        game.pump(0.1)
        continue
    shooter.fire(aim(shooter.own[0:3], v))
    shots += 1
    game.pump(0.11)
    if kills(third, start[third.name], victim.slot, shooter.slot):
        killed = True
        break
game.pump(0.5)
print("%d shots fired at the victim" % shots)
check(killed, "the server killed the victim (TE_KILLED %d by %d)" % (victim.slot, shooter.slot))
check(len(kills(victim, start[victim.name], victim.slot)) == 1, "the victim was told it died")
check(len(kills(shooter, start[shooter.name], victim.slot)) == 1, "the shooter saw it once")

# dead while the server says so: state 11 in the snapshots, shots dropped, own KILLED ignored
mark = third.mark()
states = set()
for k in range(10):
    victim.fire((0, 1, 0))
    game.pump(0.1)
    seen = third.others().get(victim.slot)
    if seen:
        states.add(seen[3])
victim.send(simple(KILLED, victim.slot, shooter.slot))
game.pump(0.4)
check(states == {11}, "the victim is seen dead (states %s)" % sorted(states))
check(not fires(third, victim.slot, mark), "its shots are dropped (%d)" % len(fires(third, victim.slot, mark)))
check(not kills(third, mark, victim.slot), "its late KILLED is not counted twice")

# ---- a KILLED / KILLEDPOSITION flood: one death ----
flooder = third
mark = shooter.mark()
for k in range(10):
    flooder.send(simple(KILLED, 0, shooter.slot))
    flooder.send(struct.pack("<bbb3f", KILLEDPOSITION, 0, shooter.slot, 300, 60, 300))
    time.sleep(0.01)
game.pump(0.8)
k = kills(shooter, mark, flooder.slot)
check(len(k) == 1, "a flood of 20 deaths counted once (%d TE_KILLED)" % len(k))
check(not kills(shooter, mark, flooder.slot, shooter.slot),
      "the forged deaths credit no kill to %d (the server never saw it hit)" % shooter.slot)
game.pump(4.0)   # dead for the server 3/4 of ac_deadtime, then ac_killinterval
mark = shooter.mark()
flooder.send(simple(KILLED, 0, shooter.slot))
game.pump(0.5)
check(len(kills(shooter, mark, flooder.slot)) == 1, "a death 4 s later is counted")

# ---- fire rate and magazine (gat: 100 ms, 50 rounds), shooting at the sky ----
shooter.fire((0, 1, 0), reload=1)
game.pump(2.6)   # reload: 3000 ms x .75
mark = third.mark()
for k in range(40):
    shooter.fire((0, 1, 0))
    time.sleep(0.005)
game.pump(0.5)
n = len(fires(third, shooter.slot, mark))
check(2 <= n <= 12, "40 shots in 0.2 s: %d relayed" % n)
shooter.fire((0, 1, 0), reload=1)
game.pump(2.6)
mark = third.mark()
game.pump(60 * 0.11, each=lambda k: shooter.fire((0, 1, 0)) if k % 7 == 0 else None)
game.pump(0.3)
n = len(fires(third, shooter.slot, mark))
check(45 <= n <= 52, "60 shots at the gat's rate, one magazine of 50 (+2 slack): %d relayed" % n)
shooter.fire((0, 1, 0), reload=1)
game.pump(2.6)
mark = third.mark()
for k in range(3):
    shooter.fire((0, 1, 0))
    game.pump(0.15)
game.pump(0.3)
n = len(fires(third, shooter.slot, mark))
check(n == 3, "after a reload: %d/3 relayed" % n)

# ---- building: far refused (any client), near relayed (one with a credit) ----
builder = next((c for c in clients if credits(c) > 0), clients[0])
watcher = next(c for c in clients if c is not builder)
builder.place(200, 120, 200)
game.settle([builder], 5)
bp = builder.own[0:3]
bx, by, bz = int(bp[0] % LANDGRIDSIZE / BLOCKSIZE), int(bp[1] / BLOCKSIZE), int(bp[2] % LANDGRIDSIZE / BLOCKSIZE)
sb = lambda v: (v + 128) % 256 - 128   # block coordinates are signed chars
mark = watcher.mark()
builder.send(block(sb(bx + 100), by, sb(bz)))
game.pump(0.8)
check(not [m for m in watcher.since(mark) if m[0] == BLOCKS], "a block 100 blocks away is refused")
if credits(builder) > 0:
    builder.send(block(sb(bx + 2), by + 1, sb(bz)))
    game.pump(0.8)
    check(len([m for m in watcher.since(mark) if m[0] == BLOCKS]) == 1, "a block next to the builder is relayed")
else:
    print("nobody has a build credit: near block not checked")

# ---- info requests from one IP ----
game.pump(1.0)
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.bind(("127.0.0.1", 0))
answers = 0
for k in range(100):
    probe.sendto(packet(simple(REQUESTINFO)), SERVER)
probe.settimeout(0.5)
end = time.time() + 1.5
while time.time() < end:
    try:
        data, addr = probe.recvfrom(65536)
        answers += sum(1 for m in messages(data) if m and m[0] == INFO)
    except socket.timeout:
        break
check(1 <= answers <= 30, "100 info requests at once: %d answered" % answers)
game.pump(2.0)
probe.sendto(packet(simple(REQUESTINFO)), SERVER)
probe.settimeout(2.0)
try:
    data, addr = probe.recvfrom(65536)
    alive = any(m and m[0] == INFO for m in messages(data))
except socket.timeout:
    alive = False
check(alive, "answered again a moment later")

# ---- phase 3a: the server decides the hits of a client that knows it ----
newcl = Client("Dave", OLD | HELLOCAP_SERVERHITS)
if not newcl.join(trailer, clients):
    print("FAIL: Dave could not join"); sys.exit(1)
game.add(newcl)
game.pump(1.0)
for cl in clients:
    teams.update(cl.teams)
print("Dave joined as slot %d (team %d)" % (newcl.slot, teams.get(newcl.slot, 0)))
check(any(a == 1 for a, b in newcl.simples(0, HITMODE)), "the new client is told the server decides hits (HITMODE 1)")
check(all(not c.simples(0, HITMODE) for c in clients[:3]), "the others never get HITMODE")
# a shooter of another team (free for all: anyone), and a watcher
gunner = next(c for c in clients[:3] if teams.get(c.slot, 0) != teams.get(newcl.slot, 0) or teams.get(c.slot, 0) == 0)
eye = next(c for c in clients[:3] if c is not gunner)
gunner.weapon = 3
print("new victim %d, shooter %d, watcher %d" % (newcl.slot, gunner.slot, eye.slot))
hits = lambda since: newcl.simples(since, SERVERHIT)


def place(vp, sp):
    """victim at vp, shooter at sp (x, z), watcher away; they land; where the shooter sees
    the victim, and where the server has the shooter"""
    newcl.place(vp[0], 120, vp[1])
    gunner.place(sp[0], 120, sp[1])
    eye.place(320, 120, 320)
    game.settle(clients, 6)
    return gunner.others().get(newcl.slot), gunner.own and gunner.own[0:3]


v, g = place((150, 150), (150, 210))   # 60 units apart
if not v or not g:
    print("FAIL: no positions for the new victim or its shooter"); sys.exit(1)
gunner.fire((0, 1, 0), reload=1)   # a full magazine
game.pump(2.6)

# a shot that says it comes from next to the victim while its shooter is 60 units
# away: it leaves from the shooter's place (the server's), and misses
mark, markv = eye.mark(), newcl.mark()
for k in range(5):
    gunner.fire((1, 0, 0), pos=(v[0] - 0.3, v[1], v[2]))
    game.pump(0.15)
game.pump(0.5)
f = fires(eye, gunner.slot, mark)
sent = [struct.unpack_from("<3f", m, 15) for m in f]
check(f and all(wrapdist(p, g) < 1 for p in sent),
      "shots that say they come from next to the victim leave from where the server has their shooter (%s)" % (
          sent[:1] and [round(c, 1) for c in sent[0]]))
check(not hits(markv), "and do no damage (%d SERVERHIT)" % len(hits(markv)))

# the shooter next to the victim (2 units on x), level with it
v, g = place((150, 150), (148, 150))
if not v or not g:
    print("FAIL: no positions for the new victim or its shooter"); sys.exit(1)
print("victim at %.1f %.1f %.1f, shooter at %.1f %.1f %.1f" % (v[0], v[1], v[2], g[0], g[1], g[2]))

# shots that don't aim at it: away from it, and 3 units beside it
mark, markv = eye.mark(), newcl.mark()
for d in [(-1, 0, 0)] * 4 + [aim(g, (v[0], v[1], v[2] + 3))] * 4:
    gunner.fire(d)
    game.pump(0.15)
game.pump(0.6)
check(len(fires(eye, gunner.slot, mark)) == 8, "the shots that miss are relayed (%d/8)" % len(fires(eye, gunner.slot, mark)))
check(not hits(markv) and not kills(eye, mark, newcl.slot),
      "a shooter not aiming at the victim does no damage (%d SERVERHIT)" % len(hits(markv)))

# its own KILLED blaming the shooter: the server decides, nothing happens
mark = eye.mark()
newcl.send(simple(KILLED, 0, gunner.slot))
game.pump(0.5)
check(not kills(eye, mark, newcl.slot), "a KILLED from the new client blaming %d is ignored" % gunner.slot)

# real shots, from where the shooter is, at the victim's body
mark, markv = eye.mark(), newcl.mark()
shots = 0
for k in range(40):
    gunner.fire(aim(g, v))
    shots += 1
    game.pump(0.15)
    if kills(eye, mark, newcl.slot):
        break
game.pump(0.5)
got = hits(markv)
print("%d shots, SERVERHIT (by, life): %s" % (shots, got))
check(got and all(by == gunner.slot for by, life in got), "aimed shots hurt the victim (%d SERVERHIT from %d)" % (len(got), gunner.slot))
check(all(got[i][1] >= got[i + 1][1] for i in range(len(got) - 1)) and got and got[-1][1] == 0,
      "its life only goes down, to 0")
check([e for e in eye.events(mark, TE_PAIN) if e["player"] == newcl.slot], "the others hear its pain (TE_PAIN)")
check(len(kills(eye, mark, newcl.slot)) == 1 and kills(eye, mark, newcl.slot, gunner.slot),
      "the server killed it, credited to %d" % gunner.slot)
check(kills(newcl, markv, newcl.slot, gunner.slot), "the victim was told")

if LOG:
    time.sleep(0.3)
    log = open(LOG, errors="replace").read()
    # (no "kill" line: in phase 3a a death the server decides is no event)
    for kind in ("death", "fire", "ammo", "build", "ip"):
        check("anticheat: %s:" % kind in log, "server log has 'anticheat: %s:'" % kind)
else:
    print("no server log given: log lines not checked")
for c in clients:
    if hasattr(c, "client"):
        c.send(simple(EXIT))
print("ALL OK" if game.ok else "SOME CHECKS FAILED")
sys.exit(0 if game.ok else 1)
