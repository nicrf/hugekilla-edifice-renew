"""Stable player slots and numbered positions (roadmap M11 phase 3b, docs/M11.md):
    HK_ANTICHEAT_LOG=1 HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_slots.py server.log
Four clients of this build (HELLOCAP_SERVERHITS, STABLESLOTS, POSSEQ) join a
local server: slots 1 to 4.
 - The one in slot 2 leaves: the others get DELETEPLAYER with PLAYERLEFT_KEEPSLOTS,
   keep their numbers (their positions are still relayed under them) and their
   state (the life the server counts for the victim goes on down from where it
   was, its shooter still credited under its number).
 - A new client takes the free slot 2, and is told about everyone under their
   numbers (PLAYERNAMES: names, teams).
 - A client of an older build (no capability byte) joins: slot 5. Then slot 2
   leaves again: with an older client in the game the server renumbers as before
   (DELETEPLAYER without the flag); everyone after slot 2 moves down one, the
   server's state with them (the victim's life, its shooter's number).
 - Numbered positions: an older (overtaken) one, a duplicate and one without its
   number are dropped, the next one is used; a burst of positions is rate
   limited. The older client's positions (no numbers) still move it.
The server log (its stderr) must show the drops."""
import math, os, socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE, FIREGUN = 1, 2, 3, 5, 10
REQUESTINFO, ENTER, EXIT, PORTACK, PLAYERNAMES, JOINGAME, DELETEPLAYER, PING, PINGREPLY = 1, 2, 3, 4, 6, 8, 11, 12, 13
KILLED, CHANGETEAM, SERVERHIT = 19, 20, 28
HELLOCAP_SERVERHITS, HELLOCAP_STABLESLOTS, HELLOCAP_POSSEQ = 8, 16, 32
PLAYERLEFT_KEEPSLOTS = 0x100
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
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


def position(x, y, z, state=10, weapon=0, seq=None):
    q = lambda v: int(v % LANDGRIDSIZE / LANDGRIDSIZE * 65536) & 0xFFFF
    m = struct.pack("<bbbbbBBHHHbbb", PLAYERPOSITION, 0, weapon, 0, state, 128, 128, q(x), q(y), q(z), 0, 0, 0)
    if seq is not None:
        m += struct.pack("<H", seq & 0xFFFF)
    return m


def firegun(pos, d, reload=0):
    return struct.pack("<bbb3f3f", FIREGUN, 0, reload, d[0], d[1], d[2], pos[0], pos[1], pos[2])


def player_names(m):
    """PLAYERNAMES: [(number, kills, deaths, team, name)]"""
    out, pos = [], 2
    for _ in range(m[1]):
        if pos + 11 > len(m):
            break
        num, kills, deaths, team, n = struct.unpack_from("<BiibB", m, pos)
        out.append((num, kills, deaths, team, m[pos + 11:pos + 11 + n]))
        pos += 11 + n
    return out


class Client:
    def __init__(self, name, caps=None):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.caps = caps     # None: an older build, no capability byte
        self.seq = 0
        self.lastid = 0
        self.got = []        # every message received
        self.pos = {}        # player -> (x, y, z, state), from relayed positions
        self.teams = {}      # player -> team, from CHANGETEAM

    def note(self, m):
        self.got.append(m)
        if m[0] == SIMPLE and len(m) >= 12:
            t, msg, msg2 = m[1], *struct.unpack_from("<II", m, 4)
            if t == CHANGETEAM:
                self.teams[msg] = msg2
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
            if t == DELETEPLAYER:
                # what a client does: renumbered unless the server keeps the slots
                self.pos = {}
                if not msg2 & PLAYERLEFT_KEEPSLOTS:
                    if msg < self.slot:
                        self.slot -= 1
                    self.teams = {(p - 1 if p > msg else p): t for p, t in self.teams.items() if p != msg}
                else:
                    self.teams.pop(msg, None)
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
        if not hasattr(self, "client") or self.s is None:
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
        hello = trailer if self.caps is None else trailer + bytes([self.caps])
        self.s.sendto(packet(bytes([REQUESTENTER, len(self.name)]) + self.name + b"\x00" + hello), SERVER)
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

    def at(self, x, y, z, state=10, seq=None):
        """its position, numbered if it is a client of this build"""
        if self.caps is not None and self.caps & HELLOCAP_POSSEQ:
            self.seq += 1
            seq = self.seq if seq is None else seq
        self.send(position(x, y, z, state, seq=seq))

    def leave(self):
        for _ in range(3):
            self.send(simple(EXIT))
        self.s.close()
        self.s = None


clients = []


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


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


def near(p, x, z, tol=1.0):
    return p is not None and abs(p[0] - x) < tol and abs(p[2] - z) < tol


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
caps = t[11 + vlen + 4] if len(t) > 11 + vlen + 4 else 0
check(caps & HELLOCAP_STABLESLOTS, "INFO says the server keeps slots")

NEW = HELLOCAP_SERVERHITS | HELLOCAP_STABLESLOTS | HELLOCAP_POSSEQ
for name in (b"Alice", b"Bob", b"Carol", b"Dan"):
    c = Client(name, NEW)
    if not c.join(trailer, clients):
        print("FAIL: %s could not join" % name.decode()); sys.exit(1)
    clients.append(c)
    print("%s joined as slot %d" % (name.decode(), c.slot))
alice, bob, carol, dan = clients
check([c.slot for c in clients] == [1, 2, 3, 4], "slots 1 to 4")
pump(1.0)
teams = {}
for cl in clients:
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
hits = lambda since: [struct.unpack_from("<II", m, 4) for m in victim.got[since:] if is_simple(m, SERVERHIT)]


def place(wait=1.5):
    """victim, shooter 2 units on x from it, the others away; returns the server's victim and shooter"""
    for _ in range(4):
        victim.at(150, 60, 150)
        shooter.at(148, 60, 150)
        for k, c in enumerate(clients):
            if c not in (victim, shooter):
                c.at(320 + 20 * k, 60, 320)
        pump(0.05)
    pump(wait)
    return eye.pos.get(victim.slot), eye.pos.get(shooter.slot)


def shoot(n):
    """n aimed shots at the victim; returns its SERVERHITs (by, life)"""
    v, g = place()
    if not v or not g:
        return None
    origin = (g[0], g[1], g[2])
    d = (v[0] - origin[0], v[1] - 1 - (origin[1] - 0.2), v[2] - origin[2])
    l = math.sqrt(sum(x * x for x in d))
    d = tuple(x / l for x in d)
    mark = len(victim.got)
    for _ in range(n):
        shooter.send(firegun(origin, d))
        pump(0.15)
    pump(0.5)
    return hits(mark)


got = shoot(3)   # 7 shots in all: the gat holds 50
print("before the leave, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and 0 < got[-1][1] < 100,
      "the victim is hurt by %d, alive" % shooter.slot)
life1 = got[-1][1] if got else 100

# ---- slot 2 leaves: everyone keeps its number ----
marks = [len(c.got) for c in clients]
bob.leave()
clients.remove(bob)
pump(1.0)
for c, mk in zip((alice, carol, dan), (marks[0], marks[2], marks[3])):
    keeps = [m for m in c.got[mk:] if is_simple(m, DELETEPLAYER, 2)]
    check(len(keeps) == 1 and struct.unpack_from("<I", keeps[0], 8)[0] & PLAYERLEFT_KEEPSLOTS,
          "%s: DELETEPLAYER 2 keeps the slots" % c.name.decode())
check([c.slot for c in clients] == [1, 3, 4], "the others keep slots 1, 3, 4")
carol.at(222, 60, 111)
dan.at(333, 60, 222)
pump(0.6)
check(near(alice.pos.get(3), 222, 111) and near(alice.pos.get(4), 333, 222),
      "their positions are relayed under 3 and 4 (%s, %s)" % (alice.pos.get(3), alice.pos.get(4)))
check(2 not in alice.pos, "and nothing under the free slot 2")
got = shoot(2)
print("after the leave, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and got[0][1] < life1,
      "the server's state stayed with them: the victim's life goes on down from %d, hit by %d" % (life1, shooter.slot))
life2 = got[-1][1] if got else life1

# ---- a new client takes the free slot ----
mark = len(alice.got)
erin = Client(b"Erin", NEW)
if not erin.join(trailer, clients):
    print("FAIL: Erin could not join"); sys.exit(1)
clients.append(erin)
pump(0.5)
check(erin.slot == 2, "the new client takes the free slot 2 (got %d)" % erin.slot)
names = [player_names(m) for m in erin.got if m[0] == PLAYERNAMES]
listed = {n[0]: (n[4], n[3]) for l in names for n in l}
print("Erin's PLAYERNAMES:", listed)
check(listed.get(1, (0,))[0] == b"Alice" and listed.get(3, (0,))[0] == b"Carol" and listed.get(4, (0,))[0] == b"Dan",
      "it is told about the others under their numbers")
check(all(listed.get(c.slot, (0, -1))[1] == teams.get(c.slot, 0) for c in (alice, carol, dan)),
      "with their teams")
joined = [n for m in alice.got[mark:] if m[0] == PLAYERNAMES for n in player_names(m)]
check(any(n[0] == 2 and n[4] == b"Erin" for n in joined), "the others are told Erin is 2")

# ---- numbered positions ----
mark = len(alice.got)
carol.at(200, 60, 200, seq=1000)
pump(0.4)
carol.at(250, 60, 250, seq=999)     # overtaken on the way
carol.at(260, 60, 260, seq=1000)    # duplicate
carol.send(position(270, 60, 270))  # without its number
pump(0.5)
check(near(alice.pos.get(3), 200, 200), "older, duplicate and unnumbered positions are dropped (%s)" % (alice.pos.get(3),))
carol.at(205, 60, 205, seq=1001)
pump(0.4)
check(near(alice.pos.get(3), 205, 205), "the next one is used (%s)" % (alice.pos.get(3),))
carol.seq = 1001
# a burst: more than ac_posrate allows, the last ones dropped
for k in range(60):
    alice.at(100 + k, 60, 100)
pump(0.5)
check(carol.pos.get(1) is not None and not near(carol.pos.get(1), 159, 100),
      "a burst of 60 positions is cut short (%s)" % (carol.pos.get(1),))

# ---- an older client: the server renumbers when slot 2 leaves ----
frank = Client(b"Frank")   # no capability byte
if not frank.join(trailer, clients):
    print("FAIL: Frank could not join"); sys.exit(1)
clients.append(frank)
check(frank.slot == 5, "the older client joins as slot 5 (got %d)" % frank.slot)
frank.send(position(400, 60, 400))
pump(0.6)
check(near(alice.pos.get(5), 400, 400), "its positions (not numbered) move it")
oldvictim, oldshooter = victim.slot, shooter.slot
marks = {c.name: len(c.got) for c in clients}
erin.leave()
clients.remove(erin)
pump(1.0)
for c in clients:
    dels = [m for m in c.got[marks[c.name]:] if is_simple(m, DELETEPLAYER, 2)]
    check(len(dels) == 1 and not struct.unpack_from("<I", dels[0], 8)[0] & PLAYERLEFT_KEEPSLOTS,
          "%s: DELETEPLAYER 2 renumbers (an older client is in the game)" % c.name.decode())
check([c.slot for c in clients] == [1, 2, 3, 4], "Alice 1, Carol 2, Dan 3, Frank 4 (%s)" % [c.slot for c in clients])
carol.at(222, 60, 111)
dan.at(333, 60, 222)
frank.send(position(444, 60, 333))
pump(0.6)
check(near(alice.pos.get(2), 222, 111) and near(alice.pos.get(3), 333, 222) and near(alice.pos.get(4), 444, 333),
      "positions relayed under the new numbers (%s)" % ({k: alice.pos.get(k) for k in (2, 3, 4)},))
check(near(frank.pos.get(2), 222, 111), "the older client sees them there too")
got = shoot(2)
print("after the renumbering, SERVERHIT (by, life):", got)
check(got and all(by == shooter.slot for by, life in got) and got[0][1] < life2,
      "the victim's life moved with it (goes on down from %d), its shooter is now %d (was %d)" % (life2, shooter.slot, oldshooter))

if LOG:
    time.sleep(0.3)
    log = open(LOG, errors="replace").read()
    for text in ("too fast", "without its number", "position 999 after 1000", "position 1000 after 1000"):
        check(text in log, "server log has '%s'" % text)
else:
    print("no server log given: log lines not checked")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
