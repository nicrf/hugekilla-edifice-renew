"""The other players in timestamped delta snapshots (docs/M16.md, phase 4):
    HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=0 sv_authmove=1" HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_snapshot.py server.log
Three clients join a local server: two announce HELLOCAP_INPUTS and
HELLOCAP_SNAPSHOTS (the second capability byte), send input commands and ack the
newest SNAPSHOT they have in them (INPUTCMD acksnap); one is a 0.7 build that sends
its positions (PLAYERPOSITION), in the same game.
 - The new clients are told so (NETSYNCMODE 256|512); after that they get no
   PLAYERPOSITION, the old one still does (of the new ones too).
 - SNAPSHOT at 30 Hz: complete (basetick 0) until the client acks one, then deltas
   on the newest it acked; never its own player in it.
 - The old client is in them where it says it is; each new one sees the other where
   the server has it (the other's own exact state of the same tick, to the
   quantization: the velocity of a fall too, no longer wrapped on a char).
 - An ack held back: deltas on it while the server keeps it (32 snapshots), then
   complete ones; an ack of 0 (the client lost its snapshots): complete ones; acks
   again: deltas again.
The server log (its stderr) must show no anti-cheat event."""
import math, os, socket, struct, sys, time

SERVER = ("127.0.0.1", int(os.environ.get("HK_TEST_PORT", "27020")))
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE = 1, 2, 3, 5
INPUTCMD, SNAPSHOT = 21, 22
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
NETSYNCMODE = 41
HELLOCAP_INPUTS, HELLOCAP_SNAPSHOTS = 256, 512
MOVEKEY_UP, MOVECMD_ALIVE = 1, 1
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
SNAPRING = 32
TICK = 1 / 60
LOG = sys.argv[1] if len(sys.argv) > 1 else None
# SNAPSHOT entries (netsync.h): mask bits and their fields
F_POS, F_VEL, F_VIEW, F_KEYS, F_STATE, F_ROLL, F_GONE = 1, 2, 4, 8, 16, 32, 128
FIELDS = ((F_POS, "<HHh", ("x", "z", "y")), (F_VEL, "<hhh", ("vx", "vy", "vz")), (F_VIEW, "<Hh", ("yaw", "pitch")),
          (F_KEYS, "<B", ("keys",)), (F_STATE, "<BBB", ("weapon", "state", "flags")), (F_ROLL, "<b", ("roll",)))


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


def command(keys=0, alive=True):
    return struct.pack("<BbbHhBBB", keys, 0, 0, 0, 0, 3, MOVECMD_ALIVE if alive else 0, 17)


def newer(a, b):
    return 0 < (a - b) & 0xFFFFFFFF < 0x80000000


def wrapdist(a, b):
    d = [(b[i] - a[i] + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2 for i in (0, 2)]
    return math.hypot(d[0], d[1], b[1] - a[1])


class Client:
    def __init__(self, name, snapshots):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.snapshots = snapshots
        self.lastid = 0
        self.got = []
        self.pos = {}         # player -> (x, y, z, state), from PLAYERPOSITION
        self.positions = 0    # PLAYERPOSITIONs received since the count was reset
        self.syncmode = None
        self.recv = {}        # tick -> {pnum: fields}: the snapshots kept (complete)
        self.history = {}     # the same, all of them
        self.newest = None    # newest tick kept: what we ack
        self.own = {}         # tick -> our exact state (x, y, z, vx, vy, vz)
        self.log = []         # (tick, basetick, n) of every SNAPSHOT
        self.nobase = 0       # deltas on a base we don't have
        self.holdack = None   # ack this instead of the newest
        self.cmd = 1000

    def snapshot(self, m):
        tick, base, lastcmd, hasself = struct.unpack_from("<IIIB", m, 1)
        pos = 14
        if hasself:
            f = struct.unpack_from("<10fBBh", m, pos)
            self.own[tick] = f[0:6]
            pos += 44
        n = m[pos]
        pos += 1
        self.log.append((tick, base, n, len(m)))
        if base and base not in self.recv:
            self.nobase += 1
            return
        players = {k: dict(v) for k, v in self.recv[base].items()} if base else {}
        for _ in range(n):
            pnum, mask = m[pos], m[pos + 1]
            pos += 2
            if mask == F_GONE:
                del players[pnum]
                continue
            p = players.setdefault(pnum, {})
            for bit, fmt, names in FIELDS:
                if mask & bit:
                    p.update(zip(names, struct.unpack_from(fmt, m, pos)))
                    pos += struct.calcsize(fmt)
        assert pos == len(m), "SNAPSHOT of %d bytes, %d read" % (len(m), pos)
        self.recv[tick] = players
        self.history[tick] = players
        if len(self.recv) > SNAPRING:
            del self.recv[min(self.recv, key=lambda t: (t - tick + (1 << 31)) & 0xFFFFFFFF)]   # the oldest
        if self.newest is None or newer(tick, self.newest):
            self.newest = tick

    def note(self, m):
        self.got.append(m)
        if m[0] == SIMPLE and len(m) >= 12:
            t, msg, msg2 = m[1], *struct.unpack_from("<II", m, 4)
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
            if t == NETSYNCMODE:
                self.syncmode = msg
        if m[0] == PLAYERPOSITION and len(m) >= 16:
            x, y, z = [v * LANDGRIDSIZE / 65536 for v in struct.unpack_from("<HHH", m, 7)]
            self.pos[m[1]] = (x, y, z, m[4])
            self.positions += 1
        if m[0] == SNAPSHOT and len(m) >= 15:
            self.snapshot(m)

    def handle(self, data):
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

    def tick(self, **kw):
        """one command (with the three before it), acking the newest snapshot kept"""
        c = command(**kw)
        ack = self.holdack if self.holdack is not None else (self.newest or 0)
        self.send(struct.pack("<BBII", INPUTCMD, 4, self.cmd - 3, ack) + c * 4)
        self.cmd += 1


clients = []
ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


def pump(wait):
    end = time.time() + wait
    while time.time() < end:
        for cl in clients:
            cl.drain()
        time.sleep(0.003)


nextold = [0]


def play(secs, movers, old, oldpos, **kw):
    """the new clients send a command a tick (acking), the old one its position every 70 ms"""
    start = time.time()
    ticks = 0
    while time.time() - start < secs:
        for cl in movers:
            cl.tick(**kw)
        ticks += 1
        if time.time() >= nextold[0]:
            old.send(position(*oldpos))
            nextold[0] = time.time() + 0.07
        pump(max(0, start + ticks * TICK - time.time()))


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
caps = t[11 + vlen + 4:]
check(len(caps) >= 2 and caps[1] & (HELLOCAP_SNAPSHOTS >> 8), "INFO announces HELLOCAP_SNAPSHOTS in its second byte")

caps2 = bytes([0, (HELLOCAP_INPUTS | HELLOCAP_SNAPSHOTS) >> 8])
a, b, old = Client(b"Alpha", True), Client(b"Bravo", True), Client(b"Oldie", False)
for cl, tr in ((a, trailer + caps2), (old, trailer), (b, trailer + caps2)):
    if not cl.join(tr, clients):
        print("FAIL: %s could not join" % cl.name.decode()); sys.exit(1)
    clients.append(cl)
print("Alpha in slot %d, Oldie in slot %d, Bravo in slot %d" % (a.slot, old.slot, b.slot))
pump(1.0)
both = HELLOCAP_INPUTS | HELLOCAP_SNAPSHOTS
check(a.syncmode == both and b.syncmode == both, "the new clients get the others in snapshots (NETSYNCMODE %s, %s)" % (a.syncmode, b.syncmode))
check(old.syncmode is None, "the old client isn't told anything")

# dead a moment, then alive: the server puts them somewhere, they fall
oldpos = (700.0, 60.0, 700.0)
play(0.5, (a, b), old, oldpos, alive=False)
first = [x for x in a.log if x[2]]
check(first and first[0][1] == 0, "the first snapshot with others in it is complete (basetick %s)" % (first and first[0][1]))
for cl in (a, b, old):
    cl.positions = 0
play(3, (a, b), old, oldpos)
check(a.positions == 0 and b.positions == 0, "no PLAYERPOSITION to the new clients (%d, %d)" % (a.positions, b.positions))
check(old.positions > 20, "the old client still gets them (%d)" % old.positions)

# 30 Hz, deltas on what was acked, never itself
recent = [x for x in a.log if newer(x[0], a.log[-1][0] - 120)]
check(55 <= len(recent) <= 61, "snapshots at 30 Hz (%d in the last 2 s)" % len(recent))
deltas = [x for x in recent if x[1]]
check(len(deltas) >= len(recent) - 2, "deltas once it acks (%d of %d)" % (len(deltas), len(recent)))
lags = [(x[0] - x[1]) & 0xFFFFFFFF for x in deltas]
print("SNAPSHOT size: %.0f bytes on average as deltas (self state included), %s complete" % (
    sum(x[3] for x in deltas) / max(1, len(deltas)), [x[3] for x in a.log if not x[1]][:1]))
check(lags and max(lags) <= 2 * 6, "on a recent base (%s ticks back at most)" % (lags and max(lags)))
check(all(a.slot not in p for p in a.recv.values()) and all(b.slot not in p for p in b.recv.values()),
      "never its own player")
check(a.nobase == 0 and b.nobase == 0, "never a base it lacks (%d, %d)" % (a.nobase, b.nobase))

# where the others are
tick = a.newest
seenold = a.recv[tick].get(old.slot)
check(seenold and seenold["state"] == 10 and
      wrapdist((seenold["x"] / 128, 0, seenold["z"] / 128), (oldpos[0], 0, oldpos[2])) < 1.5,
      "the old client is in them where it says it is, on the ground the server has there (%s)" % str(seenold and (seenold["x"] / 128, seenold["y"] / 64, seenold["z"] / 128)))
same = [t for t in b.history if t in a.own and a.slot in b.history[t]]
worst, fall = 0, 0
for t in same:
    e = b.history[t][a.slot]
    own = a.own[t]
    seen = (e["x"] / 128, e["y"] / 64, e["z"] / 128)
    worst = max(worst, wrapdist(seen, own[0:3]), abs(e["vy"] / 1024 - own[4]))
    fall = min(fall, own[4])
check(len(same) > 30 and worst < 0.02,
      "each sees the other where the server has it, velocity too (%d ticks, %.4f off at most)" % (len(same), worst))
sawfall = [b.history[t][a.slot]["vy"] / 1024 for t in same if a.own[t][4] < -1]
check(all(v < -1 for v in sawfall), "a fall is seen falling (down to %.2f units a step)" % fall)
seennew = old.pos.get(a.slot)
check(seennew and wrapdist(seennew, a.own[max(a.own, key=lambda x: x)][0:3]) < 3,
      "the old client sees the new one (PLAYERPOSITION from the server's state)")

# an ack held back: deltas on it while the server has it, then complete ones
a.log = []
a.holdack = a.newest
play(1.6, (a, b), old, oldpos, keys=MOVEKEY_UP)
on = [x for x in a.log if x[1] == a.holdack]
full = [x for x in a.log if x[1] == 0]
check(len(on) >= SNAPRING - 6 and len(on) <= SNAPRING and full and min(x[0] for x in full) == max(x[0] for x in on) + 2,
      "an old ack: deltas on it %d times (the server keeps %d), then complete ones (%d)" % (len(on), SNAPRING, len(full)))
# an ack of 0: it has nothing, complete ones
a.log = []
a.holdack = 0
play(0.3, (a, b), old, oldpos)
check(a.log and all(x[1] == 0 for x in a.log[3:]), "ack 0: complete snapshots (%d)" % len(a.log))
a.holdack = None
a.log = []
play(0.5, (a, b), old, oldpos)
check(a.log and all(x[1] for x in a.log[4:]), "acking again: deltas again")
check(a.nobase == 0, "never a base it lacks")

for cl in clients:
    cl.send(simple(3))   # EXIT
if LOG:
    text = open(LOG, "rb").read().decode("utf-8", "replace")
    events = [l for l in text.splitlines() if l.startswith("anticheat: ")]
    check(not events, "no anti-cheat event (%s)" % (events[:3] or "none"))
print("PASS" if ok else "FAILED")
sys.exit(0 if ok else 1)
