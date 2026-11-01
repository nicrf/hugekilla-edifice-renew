"""Anti-cheat checks of the server (roadmap M11 phase 1, docs/M11.md):
    HK_ANTICHEAT_LOG=1 HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_anticheat.py server.log
Three clients join a local server like net_voice.py.
 - A victim of the other team is shot at point blank and never sends KILLED:
   the server kills it itself (KILLED to everyone, the victim too), keeps it
   dead (state 11 relayed, its shots dropped) whatever it reports.
 - A flood of KILLED / KILLEDPOSITION from one client: one death counted.
 - Shots faster than the gat allows, or more than its magazine: not relayed.
 - Blocks far from their builder: refused; near it: relayed.
 - A teleport is logged; info requests from one IP are rate limited.
The server log (its stderr) must show each event."""
import os, socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
REQUESTENTER, INFO, PLAYERPOSITION, BLOCKS, SIMPLE, FIREGUN, KILLEDPOSITION = 1, 2, 3, 7, 5, 10, 14
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
KILLED, CHANGETEAM, CREDITS = 19, 20, 24
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE, BLOCKSIZE = 512, 2
LOG = sys.argv[1] if len(sys.argv) > 1 else None


def packet(*msgs):
    out = b"\x00"
    for m in msgs:
        out += struct.pack("<H", len(m)) + m
    return out


def simple(type2, msg=0, msg2=0):
    return struct.pack("<bbxxII", SIMPLE, type2, msg, msg2)


def messages(data):
    pos, out = 1, []
    while pos + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, pos)
        out.append(data[pos + 2:pos + 2 + n])
        pos += 2 + n
    return out


def checksum(data):
    c = [0, 0, 0, 0]
    for i, b in enumerate(data):
        c[i & 3] ^= b
    return bytes(c)


def position(x, y, z, state=10, weapon=0):
    q = lambda v: int(v % LANDGRIDSIZE / LANDGRIDSIZE * 65536) & 0xFFFF
    return struct.pack("<bbbbbBBHHHbbb", PLAYERPOSITION, 0, weapon, 0, state, 128, 128, q(x), q(y), q(z), 0, 0, 0)


def firegun(pos, d, reload=0):
    return struct.pack("<bbb3f3f", FIREGUN, 0, reload, d[0], d[1], d[2], pos[0], pos[1], pos[2])


def block(x, y, z, delete=0):
    return struct.pack("<bI", BLOCKS, 1) + struct.pack("<bbbbbbbbb", x, y, z, 1, 0, 0, 0, 0, delete)


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.got = []        # every message received
        self.pos = {}        # player -> (x, y, z, state), from relayed positions
        self.teams = {}      # player -> team, from CHANGETEAM
        self.credits = 0     # build credits (CREDITS 0)

    def note(self, m):
        self.got.append(m)
        if m[0] == SIMPLE and len(m) >= 12:
            t, msg, msg2 = m[1], *struct.unpack_from("<II", m, 4)
            if t == CHANGETEAM:
                self.teams[msg] = msg2
            if t == CREDITS and msg == 0:
                self.credits = msg2
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
        if m[0] == PLAYERPOSITION and len(m) >= 16:
            x, y, z = [v * LANDGRIDSIZE / 65536 for v in struct.unpack_from("<HHH", m, 7)]
            self.pos[m[1]] = (x, y, z, m[4])

    def handle(self, data):
        """acks reliable parts, answers pings, notes every message"""
        kind = data[0]
        if kind in (RELIABLE, LASTRELIABLE) and len(data) >= 9:
            body = data[5:]
            rid, num = struct.unpack_from("<HH", body, 0)
            self.s.sendto(bytes([ACK]) + body[:4], self.client)
            if rid != (self.lastid + 1) & 0xFFFF:
                return
            self.parts[num] = body[4:]
            if kind == LASTRELIABLE:
                self.lastid = rid
                buf = b"".join(self.parts[i] for i in sorted(self.parts))
                self.parts = {}
                pos = 0
                while pos + 4 <= len(buf):
                    (n,) = struct.unpack_from("<I", buf, pos)
                    m = buf[pos + 4:pos + 4 + n]
                    if m:
                        self.note(m)
                    pos += 4 + n
        elif kind == 0:
            for m in messages(data):
                if m:
                    self.note(m)

    def drain(self):
        if not hasattr(self, "client"):
            return
        self.s.setblocking(False)
        try:
            while True:
                data, addr = self.s.recvfrom(65536)
                if data:
                    self.handle(data)
        except (BlockingIOError, socket.error):
            pass

    def join(self, trailer, others=()):
        self.s.sendto(packet(bytes([REQUESTENTER, len(self.name)]) + self.name + b"\x00" + trailer), SERVER)
        port = None
        end = time.time() + 3
        while time.time() < end and port is None:
            for o in others:
                o.drain()
            self.s.settimeout(0.5)
            try:
                data, addr = self.s.recvfrom(65536)
            except socket.timeout:
                continue
            if data and data[0] == 0:
                for m in messages(data):
                    if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
                        p, self.slot = struct.unpack_from("<II", m, 4)
                        port = socket.ntohs(p & 0xFFFF)
        if port is None:
            return False
        self.client = (SERVER[0], port)
        self.parts = {}
        payload = simple(PORTACK, socket.htons(port))
        body = struct.pack("<HHI", 1, 0, len(payload)) + payload
        self.s.sendto(bytes([LASTRELIABLE]) + checksum(body) + body, self.client)
        end = time.time() + 15
        while time.time() < end:
            for o in others:
                o.drain()
            self.drain()
            if any(m[0] == SIMPLE and len(m) >= 12 and m[1] == JOINGAME and
                   struct.unpack_from("<I", m, 4)[0] == self.slot for m in self.got):
                return True
            time.sleep(0.05)
        return False

    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)


def pump(wait):
    end = time.time() + wait
    while time.time() < end:
        for cl in clients:
            cl.drain()
        time.sleep(0.01)


def count(cl, pred, since=0):
    return sum(1 for m in cl.got[since:] if pred(m))


def is_simple(m, t, msg=None, msg2=None):
    if m[0] != SIMPLE or len(m) < 12 or m[1] != t:
        return False
    a, b = struct.unpack_from("<II", m, 4)
    return (msg is None or a == msg) and (msg2 is None or b == msg2)


def fires_from(slot):
    return lambda m: m[0] == FIREGUN and len(m) >= 27 and m[1] == slot


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


# version trailer from INFO
probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
probe.bind(("127.0.0.1", 0))
info = None
for _ in range(40):
    probe.sendto(packet(simple(REQUESTINFO)), SERVER)
    probe.settimeout(1.0)
    try:
        data, addr = probe.recvfrom(65536)
        info = next((m for m in messages(data) if m and m[0] == INFO), None)
    except socket.timeout:
        pass
    if info:
        break
if not info:
    print("FAIL: no INFO reply"); sys.exit(1)
t = info[2 + info[1] + 1:]
_, proto, layout, vlen = struct.unpack_from("<IHIB", t, 0)
trailer = t[:11 + vlen]

clients = [Client(n) for n in (b"Alice", b"Bob", b"Carol")]
for n, c in enumerate(clients):
    if not c.join(trailer, clients[:n]):
        print("FAIL: %s could not join" % c.name.decode()); sys.exit(1)
    print("%s joined as slot %d" % (c.name.decode(), c.slot))
pump(1.0)
teams = {}
for cl in clients:
    teams.update(cl.teams)
print("teams:", teams, " credits:", [cl.credits for cl in clients])

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

# ---- the victim never sends KILLED: the server kills it ----
for _ in range(3):
    victim.send(position(100, 60, 100))
    shooter.send(position(101, 60, 100))
    third.send(position(300, 60, 300))
    pump(0.05)
pump(1.5)   # the host sets them on the ground
start = [len(cl.got) for cl in clients]
killed = False
shots = 0
for k in range(60):
    vp = shooter.pos.get(victim.slot)
    if not vp:
        pump(0.1)
        continue
    x, y, z, state = vp
    # point blank, level with the victim (the bullet starts .2 lower)
    shooter.send(firegun((x - 0.3, y, z), (1, 0, 0)))
    shots += 1
    pump(0.11)
    if count(third, lambda m: is_simple(m, KILLED, victim.slot, shooter.slot), start[2]):
        killed = True
        break
pump(0.5)
print("%d shots fired at the victim" % shots)
check(killed, "the server killed the victim (KILLED %d by %d)" % (victim.slot, shooter.slot))
check(count(victim, lambda m: is_simple(m, KILLED, victim.slot), start[clients.index(victim)]) == 1,
      "the victim was told it died")
check(count(shooter, lambda m: is_simple(m, KILLED, victim.slot), start[clients.index(shooter)]) == 1,
      "the shooter saw it once")

# dead while the server says so: state 11 relayed, shots dropped, own KILLED ignored
mark = len(third.got)
for k in range(10):
    victim.send(position(100, 60, 100), firegun((100, 60, 100), (0, 1, 0)))
    pump(0.1)
victim.send(simple(KILLED, victim.slot, shooter.slot))
pump(0.4)
states = [m[4] for m in third.got[mark:] if m[0] == PLAYERPOSITION and len(m) >= 16 and m[1] == victim.slot]
check(states and all(s == 11 for s in states), "the victim is relayed dead (states %s)" % sorted(set(states)))
check(count(third, fires_from(victim.slot), mark) == 0, "its shots are dropped (%d)" % count(third, fires_from(victim.slot), mark))
check(count(third, lambda m: is_simple(m, KILLED, victim.slot), mark) == 0, "its late KILLED is not counted twice")

# ---- a KILLED / KILLEDPOSITION flood: one death ----
flooder = third
mark = [len(cl.got) for cl in clients]
for k in range(10):
    flooder.send(simple(KILLED, 0, shooter.slot))
    flooder.send(struct.pack("<bbb3f", KILLEDPOSITION, 0, shooter.slot, 300, 60, 300))
    time.sleep(0.01)
pump(0.8)
kills = count(shooter, lambda m: is_simple(m, KILLED, flooder.slot), mark[clients.index(shooter)])
kps = count(shooter, lambda m: m[0] == KILLEDPOSITION and len(m) >= 15 and m[1] == flooder.slot, mark[clients.index(shooter)])
check(kills + kps == 1, "a flood of 20 deaths counted once (%d KILLED, %d KILLEDPOSITION)" % (kills, kps))
pump(2.1)
mark = len(shooter.got)
flooder.send(simple(KILLED, 0, shooter.slot))
pump(0.5)
check(count(shooter, lambda m: is_simple(m, KILLED, flooder.slot), mark) == 1, "a death 2 s later is counted")

# ---- fire rate and magazine (gat: 100 ms, 50 rounds), shooting at the sky ----
sp = third.pos.get(shooter.slot, (101, 60, 100, 10))
up = (sp[0], sp[1], sp[2])
shooter.send(firegun(up, (0, 1, 0), reload=1))
pump(2.6)   # reload: 3000 ms x .75
mark = len(third.got)
for k in range(40):
    shooter.send(firegun(up, (0, 1, 0)))
    time.sleep(0.005)
pump(0.5)
n = count(third, fires_from(shooter.slot), mark)
check(2 <= n <= 12, "40 shots in 0.2 s: %d relayed" % n)

shooter.send(firegun(up, (0, 1, 0), reload=1))
pump(2.6)
mark = len(third.got)
for k in range(60):
    shooter.send(firegun(up, (0, 1, 0)))
    pump(0.11)
pump(0.3)
n = count(third, fires_from(shooter.slot), mark)
check(45 <= n <= 52, "60 shots at the gat's rate, one magazine of 50 (+2 slack): %d relayed" % n)
shooter.send(firegun(up, (0, 1, 0), reload=1))
pump(2.6)
mark = len(third.got)
for k in range(3):
    shooter.send(firegun(up, (0, 1, 0)))
    pump(0.15)
pump(0.3)
n = count(third, fires_from(shooter.slot), mark)
check(n == 3, "after a reload: %d/3 relayed" % n)

# ---- building: far refused (any client), near relayed (one with a credit) ----
builder = next((c for c in clients if c.credits > 0), clients[0])
watcher = next(c for c in clients if c is not builder)
for _ in range(3):
    builder.send(position(200, 60, 200))
    pump(0.05)
pump(1.0)
bp = watcher.pos.get(builder.slot, (200, 60, 200, 10))
bx, by, bz = int(bp[0] / BLOCKSIZE), int(bp[1] / BLOCKSIZE), int(bp[2] / BLOCKSIZE)
sb = lambda v: (v + 128) % 256 - 128   # block coordinates are signed chars
blocks = lambda m: m[0] == BLOCKS
mark = len(watcher.got)
builder.send(block(sb(bx + 100), by, sb(bz)))
pump(0.8)
check(count(watcher, blocks, mark) == 0, "a block 100 blocks away is refused")
if builder.credits > 0:
    builder.send(block(sb(bx + 2), by + 1, sb(bz)))
    pump(0.8)
    check(count(watcher, blocks, mark) == 1, "a block next to the builder is relayed")
else:
    print("nobody has a build credit: near block not checked")

# ---- a teleport, logged ----
for p in ((300, 60, 300), (300, 60, 300), (400, 60, 400)):
    third.send(position(*p))
    pump(0.35)

# ---- info requests from one IP ----
pump(1.0)
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
pump(2.0)
probe.sendto(packet(simple(REQUESTINFO)), SERVER)
probe.settimeout(2.0)
try:
    data, addr = probe.recvfrom(65536)
    alive = any(m and m[0] == INFO for m in messages(data))
except socket.timeout:
    alive = False
check(alive, "answered again a moment later")

if LOG:
    time.sleep(0.3)
    log = open(LOG, errors="replace").read()
    for kind in ("kill", "death", "fire", "ammo", "build", "move", "ip"):
        check("anticheat: %s:" % kind in log, "server log has 'anticheat: %s:'" % kind)
else:
    print("no server log given: log lines not checked")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
