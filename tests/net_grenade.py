"""Shots at a tick, grenades the server owns, timestamped events (docs/M16.md, phase 5):
    HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=0" HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_grenade.py server.log
Three clients join a local server: two announce HELLOCAP_TICKEVENTS (with
HELLOCAP_INPUTS and HELLOCAP_SNAPSHOTS: the second capability byte), one is a 0.7
build that sends its positions (PLAYERPOSITION). All of them have smoke grenades.
 - The new ones are told so (NETSYNCMODE 256|512|1024), the old one nothing.
 - The thrower throws a smoke grenade (THROWSMOKE + FireTick, its number 88): it
   gets the server's object (UPDATESERVEROBJECT, SMOKECLOUD), then TE_SMOKE naming
   that slot and its number (its predicted one makes way); the others get the
   server's object, not the TE_SMOKE.
 - It fires a bullet (FIREGUN + FireTick): the others get FIREGUN with the server's
   tick after it (the new one queues it until it draws that tick; the old one reads
   the shot alone).
 - It throws a frag grenade at its feet (FIREGUN + FireTick, number 77): every new
   client gets TE_GRENADE (the thrower too: its predicted copy takes it over),
   thrown from where the server had the thrower after the command it named, and no
   FIREGUN for it; the old client gets the FIREGUN as before. The grenade goes off
   by the thrower: TE_EXPLODE to the new ones about a fuse later, the
   thrower dies (TE_KILLED to the new ones, at the tick, KILLED to the old one) and
   the pain of the hit reaches the new one as TE_PAIN, the old one as PAIN.
The server log (its stderr) must show no anti-cheat event."""
import math, os, socket, struct, sys, time

SERVER = ("127.0.0.1", int(os.environ.get("HK_TEST_PORT", "27020")))
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE, FIREGUN = 1, 2, 3, 5, 10
UPDATESERVEROBJECT, THROWSMOKE = 12, 20
INPUTCMD, SNAPSHOT, TICKEVENT = 21, 22, 23
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
PAIN, KILLED, SMOKEMODE, NETSYNCMODE = 18, 19, 40, 41
HELLOCAP_SERVERHITS, HELLOCAP_SMOKE = 8, 128
HELLOCAP_INPUTS, HELLOCAP_SNAPSHOTS, HELLOCAP_TICKEVENTS = 256, 512, 1024
TE_KILLED, TE_PAIN, TE_GRENADE, TE_EXPLODE, TE_SMOKE = 1, 2, 3, 4, 5
SMOKECLOUD = 5
MOVECMD_ALIVE = 1
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
TICK = 1 / 60
FUSE = 1700   # sv.ini grenadeexplodetime, ms
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


def position(x, y, z, state=10, weapon=4):
    q = lambda v: int(v % LANDGRIDSIZE / LANDGRIDSIZE * 65536) & 0xFFFF
    return struct.pack("<bbbbbBBHHHbbb", PLAYERPOSITION, 0, weapon, 0, state, 128, 128, q(x), q(y), q(z), 0, 0, 0)


def command(weapon, alive=True):
    return struct.pack("<BbbHhBBB", 0, 0, 0, 0, 0, weapon, MOVECMD_ALIVE if alive else 0, 17)


def shot(kind, pos, d, viewtick, cmd, objid):
    """FIREGUN or THROWSMOKE (27 bytes), then FireTick (netsync.h)"""
    return struct.pack("<bbb3f3f", kind, 0, 0, d[0], d[1], d[2], pos[0], pos[1], pos[2]) + \
        struct.pack("<IBIH", viewtick, 0, cmd, objid)


def tickevent(m):
    """the fields of a TICKEVENT (netsync.h)"""
    tick, kind = struct.unpack_from("<IB", m, 1)
    e = dict(tick=tick, kind=kind)
    if kind == TE_KILLED:
        e.update(killed=m[6], killer=m[7], haspos=m[8])
    elif kind == TE_PAIN:
        e.update(player=m[6], sound=struct.unpack_from("<H", m, 7)[0])
    elif kind in (TE_GRENADE, TE_EXPLODE):
        e.update(player=m[6], objid=struct.unpack_from("<H", m, 7)[0], pos=struct.unpack_from("<3f", m, 9))
        if kind == TE_GRENADE:
            e.update(vel=struct.unpack_from("<3f", m, 21), ms=struct.unpack_from("<H", m, 33)[0])
    elif kind == TE_SMOKE:
        e.update(player=m[6], objid=struct.unpack_from("<H", m, 7)[0], slot=m[9])
    return e


def wrapdist(a, b):
    d = [(b[i] - a[i] + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2 for i in (0, 2)]
    return math.hypot(d[0], d[1], b[1] - a[1])


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.got = []         # (time, message) in the order they came
        self.syncmode = None
        self.newest = 0       # newest SNAPSHOT tick (what we ack)
        self.own = None       # our exact state: (x, y, z, vx, vy, vz)
        self.lastcmd = 0      # the server's last command run of ours
        self.cmd = 1000
        self.weapon = 4

    def note(self, m):
        self.got.append((time.time(), m))
        if m[0] == SIMPLE and len(m) >= 12:
            t, msg, msg2 = m[1], *struct.unpack_from("<II", m, 4)
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
            if t == NETSYNCMODE:
                self.syncmode = msg
        if m[0] == SNAPSHOT and len(m) >= 15:
            tick, base, lastcmd, hasself = struct.unpack_from("<IIIB", m, 1)
            if not self.newest or 0 < (tick - self.newest) & 0xFFFFFFFF < 0x80000000:
                self.newest = tick
                self.lastcmd = lastcmd
                if hasself:
                    self.own = struct.unpack_from("<6f", m, 14)

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
                   struct.unpack_from("<I", m, 4)[0] == self.slot for _, m in self.got):
                return True
            time.sleep(0.05)
        return False

    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)

    def tick(self):
        """one command (with the three before it), acking the newest snapshot"""
        c = command(self.weapon)
        self.send(struct.pack("<BBII", INPUTCMD, 4, self.cmd - 3, self.newest) + c * 4)
        self.cmd += 1

    def since(self, t):
        return [m for at, m in self.got if at >= t]

    def events(self, t, kind=None):
        return [tickevent(m) for m in self.since(t) if m[0] == TICKEVENT and len(m) >= 6 and
                (kind is None or m[5] == kind)]


clients = []
ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


nextold = [0]


def play(secs, movers, old, oldpos):
    """the new clients send a command a tick, the old one its position every 70 ms"""
    start = time.time()
    ticks = 0
    while time.time() - start < secs:
        for cl in movers:
            cl.tick()
        ticks += 1
        if time.time() >= nextold[0]:
            old.send(position(*oldpos))
            nextold[0] = time.time() + 0.07
        end = start + ticks * TICK
        while time.time() < end:
            for cl in clients:
                cl.drain()
            time.sleep(0.002)


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
check(len(caps) >= 2 and caps[1] & (HELLOCAP_TICKEVENTS >> 8), "INFO announces HELLOCAP_TICKEVENTS in its second byte")

new = bytes([HELLOCAP_SERVERHITS | HELLOCAP_SMOKE, (HELLOCAP_INPUTS | HELLOCAP_SNAPSHOTS | HELLOCAP_TICKEVENTS) >> 8])
thrower, watcher, old = Client(b"Thrower"), Client(b"Watcher"), Client(b"Oldie")
for cl, tr in ((thrower, trailer + new), (old, trailer + bytes([HELLOCAP_SMOKE])), (watcher, trailer + new)):
    if not cl.join(tr, clients):
        print("FAIL: %s could not join" % cl.name.decode()); sys.exit(1)
    clients.append(cl)
print("Thrower in slot %d, Oldie in slot %d, Watcher in slot %d" % (thrower.slot, old.slot, watcher.slot))
movers = (thrower, watcher)
oldpos = (700.0, 60.0, 700.0)
play(1.0, movers, old, oldpos)
mode = HELLOCAP_INPUTS | HELLOCAP_SNAPSHOTS | HELLOCAP_TICKEVENTS
check(thrower.syncmode == mode and watcher.syncmode == mode,
      "the new clients get shots at a tick and events (NETSYNCMODE %s, %s)" % (thrower.syncmode, watcher.syncmode))
check(old.syncmode is None, "the old client isn't told anything")
# alive (the server puts them somewhere), down to the ground
play(5.0, movers, old, oldpos)
check(thrower.own and abs(thrower.own[4]) < .1, "the thrower stands on the ground (%s)" % (
    thrower.own and [round(v, 3) for v in thrower.own],))
if not thrower.own:
    sys.exit(1)
here = thrower.own[0:3]

# a smoke grenade
mark = time.time()
thrower.send(shot(THROWSMOKE, here, (1, .5, 0), thrower.newest - 6, thrower.cmd - 1, 88))
play(1.0, movers, old, oldpos)
seq = [m for m in thrower.since(mark) if m[0] in (UPDATESERVEROBJECT, TICKEVENT)]
smokeat = next((i for i, m in enumerate(seq) if m[0] == UPDATESERVEROBJECT and m[5] == SMOKECLOUD), None)
slot = struct.unpack_from("<i", seq[smokeat], 1)[0] if smokeat is not None else None
te = [(i, tickevent(m)) for i, m in enumerate(seq) if m[0] == TICKEVENT and m[5] == TE_SMOKE]
check(slot is not None and te and te[0][1]["objid"] == 88 and te[0][1]["slot"] == slot and te[0][1]["player"] == thrower.slot,
      "the thrower is told the server's smoke grenade (slot %s) takes the place of its number 88 (%s)" % (
          slot, te and te[0][1]))
check(te and smokeat is not None and te[0][0] > smokeat, "after the server's object itself")
for cl in (watcher, old):
    objs = [m for m in cl.since(mark) if m[0] == UPDATESERVEROBJECT and m[5] == SMOKECLOUD]
    check(objs and struct.unpack_from("<i", objs[0], 1)[0] == slot, "%s sees the server's smoke grenade" % cl.name.decode())
check(not watcher.events(mark, TE_SMOKE), "the others aren't told about the number")

# a bullet
thrower.weapon = 3
play(0.5, movers, old, oldpos)
mark = time.time()
tick0 = thrower.newest
thrower.send(shot(FIREGUN, here, (1, 0, 0), thrower.newest - 6, thrower.cmd - 1, 0))
play(0.6, movers, old, oldpos)
wf = [m for m in watcher.since(mark) if m[0] == FIREGUN and m[1] == thrower.slot]
check(wf and len(wf[0]) == 31 and abs(((struct.unpack_from("<I", wf[0], 27)[0] - tick0 + (1 << 31)) & 0xFFFFFFFF) - (1 << 31)) < 30,
      "the new client gets the shot with the server's tick (%s bytes, tick %s near %s)" % (
          wf and len(wf[0]), wf and struct.unpack_from("<I", wf[0], 27)[0], tick0))
of = [m for m in old.since(mark) if m[0] == FIREGUN and m[1] == thrower.slot]
check(of, "the old client gets the shot (it reads its first 27 bytes)")
check(not watcher.events(mark, TE_GRENADE), "a bullet is no grenade")

# a frag grenade, at its feet: it goes off by the thrower and kills it
thrower.weapon = 4
play(0.6, movers, old, oldpos)
mark = time.time()
here = thrower.own[0:3]
throwcmd = thrower.cmd - 1
thrower.send(shot(FIREGUN, here, (0, -1, 0), thrower.newest - 6, throwcmd, 77))
play(FUSE / 1000 + 1.5, movers, old, oldpos)
for cl in (thrower, watcher):
    g = cl.events(mark, TE_GRENADE)
    check(g and g[0]["objid"] == 77 and g[0]["player"] == thrower.slot and wrapdist(g[0]["pos"], here) < .5 and
          g[0]["vel"][1] < 0 and abs(g[0]["ms"] - FUSE) < 100,
          "%s gets the server's grenade, number 77, thrown from where the thrower was (%s)" % (
              cl.name.decode(), g and (g[0]["objid"], [round(v, 2) for v in g[0]["pos"]], g[0]["ms"])))
    check(not [m for m in cl.since(mark) if m[0] == FIREGUN and m[1] == thrower.slot], "and no FIREGUN for it")
    x = cl.events(mark, TE_EXPLODE)
    check(g and x and x[0]["objid"] == 77 and abs(((x[0]["tick"] - g[0]["tick"]) & 0xFFFFFFFF) - FUSE * 60 / 1000) < 6 and
          wrapdist(x[0]["pos"], here) < 3,
          "%s: it goes off a fuse later, by the thrower (TE_EXPLODE %s ticks later, at %s)" % (
              cl.name.decode(), x and g and (x[0]["tick"] - g[0]["tick"]) & 0xFFFFFFFF,
              x and [round(v, 2) for v in x[0]["pos"]]))
of = [m for m in old.since(mark) if m[0] == FIREGUN and m[1] == thrower.slot]
check(of and abs(struct.unpack_from("<f", of[0], 7)[0] + 1) < .01, "the old client gets the throw (FIREGUN) as before")
x = watcher.events(mark, TE_EXPLODE)
k = watcher.events(mark, TE_KILLED)
check(k and k[0]["killed"] == thrower.slot and k[0]["killer"] == thrower.slot and x and k[0]["tick"] == x[0]["tick"],
      "the new client is told the thrower died, at the tick it went off (%s)" % (k and k[0]))
check(thrower.events(mark, TE_KILLED) and thrower.events(mark, TE_KILLED)[0]["killed"] == thrower.slot,
      "the thrower is told it died")
p = watcher.events(mark, TE_PAIN)
check(p and p[0]["player"] == thrower.slot and x and p[0]["tick"] == x[0]["tick"], "and hears its pain at that tick (TE_PAIN)")
ok_old = [m for m in old.since(mark) if m[0] == SIMPLE and len(m) >= 12 and m[1] == KILLED and
          struct.unpack_from("<II", m, 4) == (thrower.slot, thrower.slot)]
check(ok_old, "the old client gets KILLED as before")
check([m for m in old.since(mark) if m[0] == SIMPLE and len(m) >= 12 and m[1] == PAIN], "and PAIN")
check(not [m for m in watcher.since(mark) if m[0] == SIMPLE and len(m) >= 12 and m[1] in (KILLED, PAIN)],
      "no KILLED nor PAIN to the new one")

for cl in clients:
    cl.send(simple(3))   # EXIT
if LOG:
    text = open(LOG, "rb").read().decode("utf-8", "replace")
    events = [l for l in text.splitlines() if l.startswith("anticheat: ")]
    check(not events, "no anti-cheat event (%s)" % (events[:3] or "none"))
print("PASS" if ok else "FAILED")
sys.exit(0 if ok else 1)
