"""Smoke grenades (src/game/smoke.h) against a local server:
    HK_AUTOSTART=1 <Edifice binary> &
    python3 tests/net_smoke.py
 - INFO announces HELLOCAP_SMOKE.
 - A client with HELLOCAP_SMOKE joins alone: the server tells it it takes smoke
   grenades (SMOKEMODE 1). It throws one (THROWSMOKE, grenade in hand): it gets
   the server object (type SMOKECLOUD) flying, then landed with its age.
 - An older client (no capabilities) joins while the cloud lives: its snapshot
   has an empty slot instead, it plays on (positions relayed both ways), and
   the new client is told smoke grenades are off (SMOKEMODE 0): a throw now
   makes nothing.
 - A second new client joining then gets the cloud in its snapshot, landed, with
   an age that fits; at the end of its life the server removes it (type 0) and
   the older client never got a smoke object at all.
NET_TEST_PORT: the server's info port if not 27020 (net.ini of its HK_USERDIR)."""
import os, socket, struct, sys, time

SERVER = ("127.0.0.1", int(os.environ.get("NET_TEST_PORT", "27020")))
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE = 1, 2, 3, 5
UPDATESERVEROBJECT, SERVEROBJECTSNAPSHOT, THROWSMOKE = 12, 13, 20
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
SMOKEMODE = 40
HELLOCAP_SMOKE = 128
SMOKECLOUD, LIFETIME = 5, 25.0
MAXSERVEROBJS, OBJSIZE = 100, 43
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512


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


def throwsmoke(pos, d):
    return struct.pack("<bbb3f3f", THROWSMOKE, 0, 0, d[0], d[1], d[2], pos[0], pos[1], pos[2])


def sobj(buf, off):
    """a server object: type, pos, dir, vel, team, attached"""
    t = struct.unpack_from("<b", buf, off)[0]
    v = struct.unpack_from("<9f", buf, off + 1)
    team, att = struct.unpack_from("<bb", buf, off + 37)
    return dict(type=t, pos=v[0:3], dir=v[3:6], vel=v[6:9], team=team, attached=att)


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.got = []
        self.pos = {}

    def note(self, m):
        self.got.append((time.time(), m))
        if m[0] == SIMPLE and len(m) >= 12 and m[1] == PING:
            self.s.sendto(packet(simple(PINGREPLY, struct.unpack_from("<I", m, 4)[0], 0)), self.client)
        if m[0] == PLAYERPOSITION and len(m) >= 16:
            x, y, z = [v * LANDGRIDSIZE / 65536 for v in struct.unpack_from("<HHH", m, 7)]
            self.pos[m[1]] = (x, y, z, m[4])

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

    def join(self, hello, others=()):
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
                   struct.unpack_from("<I", m, 4)[0] == self.slot for _, m in self.got):
                return True
            time.sleep(0.05)
        return False

    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)

    def smokemodes(self):
        return [struct.unpack_from("<I", m, 4)[0] for _, m in self.got
                if m[0] == SIMPLE and len(m) >= 12 and m[1] == SMOKEMODE]

    def updates(self, since=0.0):
        """(time, slot, object) of every UPDATESERVEROBJECT"""
        out = []
        for t, m in self.got:
            if t >= since and m[0] == UPDATESERVEROBJECT and len(m) >= 5 + OBJSIZE:
                out.append((t, struct.unpack_from("<i", m, 1)[0], sobj(m, 5)))
        return out

    def snapshot(self):
        for t, m in self.got:
            if m[0] == SERVEROBJECTSNAPSHOT and len(m) >= 1 + MAXSERVEROBJS * OBJSIZE:
                return [sobj(m, 1 + i * OBJSIZE) for i in range(MAXSERVEROBJS)]
        return None


clients = []
spot = {}   # client -> where it stands (sent every pump)


def pump(wait):
    end = time.time() + wait
    while time.time() < end:
        for cl in clients:
            cl.drain()
            if cl in spot:
                cl.send(position(*spot[cl]))
        time.sleep(0.05)


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


# INFO: version trailer, then 4 bytes of details and the capability byte
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
check(caps & HELLOCAP_SMOKE, "INFO announces smoke grenades (caps %d)" % caps)

# ---- a new client alone: smoke grenades on, it throws one ----
new = Client(b"Smoky")
if not new.join(trailer + bytes([HELLOCAP_SMOKE])):
    print("FAIL: the new client could not join"); sys.exit(1)
clients.append(new)
print("new client joined as slot %d" % new.slot)
spot[new] = (100, 60, 100)
pump(2.0)   # the host sets it on the ground: its relayed position says where
check(new.smokemodes()[-1:] == [1], "the server takes smoke grenades (SMOKEMODE %s)" % new.smokemodes())
ground = new.pos.get(new.slot)
x, y, z = (ground[0], ground[1], ground[2]) if ground else (100, 60, 100)
spot[new] = (x, y, z)
pump(0.3)
thrown = time.time()
new.send(throwsmoke((x, y, z), (0.7, 0.2, 0.7)))
pump(4.0)
ups = [(t, s, o) for t, s, o in new.updates(thrown) if o["type"] == SMOKECLOUD]
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

# ---- an older client joins while it lives ----
old = Client(b"Oldtimer")
if not old.join(trailer, clients):
    print("FAIL: the older client could not join"); sys.exit(1)
clients.append(old)
spot[old] = (x + 3, y, z + 3)
pump(2.0)
snap = old.snapshot()
check(snap is not None, "the older client got its snapshot")
if snap is not None:
    check(all(o["type"] in (0, 1, 2, 3) for o in snap), "no smoke object in it (types %s)" % sorted({o["type"] for o in snap}))
    if slot >= 0:
        check(snap[slot]["type"] == 0, "slot %d is empty for it" % slot)
check(new.smokemodes()[-1:] == [0], "the new client is told smoke grenades are off (SMOKEMODE %s)" % new.smokemodes())
check(new.slot in old.pos and old.slot in new.pos, "both see each other move (positions relayed)")
mark = time.time()
new.send(throwsmoke((x, y, z), (0.7, 0.2, 0.7)))
pump(2.5)
check(not [u for u in new.updates(mark) if u[2]["type"] == SMOKECLOUD and u[1] != slot],
      "a throw now makes no new cloud")

# ---- a second new client joins: the cloud in its snapshot ----
late = Client(b"Latecomer")
if not late.join(trailer + bytes([HELLOCAP_SMOKE]), clients):
    print("FAIL: the second new client could not join"); sys.exit(1)
clients.append(late)
spot[late] = (x - 3, y, z - 3)
pump(1.0)
snap = late.snapshot()
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
pump(max(left, 0) + 3)
ends = [u for u in new.updates(mark) if u[1] == slot and u[2]["type"] == 0]
check(ends, "the server removed it after its lifetime")
check(not [u for u in old.updates() if u[2]["type"] == SMOKECLOUD], "the older client never got a smoke object")
check(old.slot in new.pos and time.time() - max(t for t, m in old.got) < 2, "the older client still plays (messages flowing)")
print("all ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
