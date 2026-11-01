"""Proximity voice chat relay (VOICE message, src/net/voicemsg.h):
    HK_AUTOSTART=1 <Edifice binary> &  then  python3 tests/net_voice.py
Three clients join a local server like net_join.py and report positions:
A and B on the very same spot, C far away. The host sets every player on the
ground or the blocks under it, and the world is random: the positions are sent
first (at y=60), the voice frames after, alone, once the host has set A and B
on the same ground. The world-edge wrap is covered by voice_test.
A sends VOICE frames with a spoofed player field: B must get them as
coming from A's slot, C must get nothing. Then B moves away and gets nothing
either; malformed VOICE messages are never relayed. Last, A stands next to the
host and sends garbage frames, which the host decodes (opus must reject them)."""
import os, socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE, VOICE = 1, 2, 3, 5, 16
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
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


def voice(player, seq, frame):
    return struct.pack("<BBHH", VOICE, player, seq, len(frame)) + frame


def position(x, y, z, state=10):
    q = lambda v: int(v / LANDGRIDSIZE * 65536) & 0xFFFF
    return struct.pack("<bbbbbBBHHHbbb", PLAYERPOSITION, 0, 0, 0, state, 128, 128, q(x), q(y), q(z), 0, 0, 0)


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.voices = []   # (player, seq, frame)
        self.hostpos = None

    def handle(self, data):
        """acks reliable parts, answers pings, collects VOICE; returns plain messages"""
        out = []
        kind = data[0]
        if kind in (RELIABLE, LASTRELIABLE) and len(data) >= 9:
            body = data[5:]
            rid, num = struct.unpack_from("<HH", body, 0)
            self.s.sendto(bytes([ACK]) + body[:4], self.client)
            if rid != (self.lastid + 1) & 0xFFFF:
                return out
            self.parts[num] = body[4:]
            if kind == LASTRELIABLE:
                self.lastid = rid
                buf = b"".join(self.parts[i] for i in sorted(self.parts))
                self.parts = {}
                pos = 0
                while pos + 4 <= len(buf):
                    (n,) = struct.unpack_from("<I", buf, pos)
                    out.append(buf[pos + 4:pos + 4 + n])
                    pos += 4 + n
        elif kind == 0:
            for m in messages(data):
                if not m:
                    continue
                if m[0] == SIMPLE and len(m) >= 12 and m[1] == PING:
                    (pid,) = struct.unpack_from("<I", m, 4)
                    self.s.sendto(packet(simple(PINGREPLY, pid, 0)), self.client)
                if m[0] == PLAYERPOSITION and len(m) >= 16 and m[1] == 0:
                    self.hostpos = [v * LANDGRIDSIZE / 65536 for v in struct.unpack_from("<HHH", m, 7)]
                if m[0] == VOICE and len(m) >= 6:
                    player, seq, n = struct.unpack_from("<BHH", m, 1)
                    self.voices.append((player, seq, m[6:]))
                out.append(m)
        return out

    def drain(self):
        """handles whatever is already queued, without waiting"""
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

    def pump(self, wait):
        end = time.time() + wait
        got = []
        while True:
            left = end - time.time()
            if left <= 0:
                break
            self.s.settimeout(left)
            try:
                data, addr = self.s.recvfrom(65536)
            except socket.timeout:
                break
            if data:
                got += self.handle(data)
        return got

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
            for m in self.pump(0.3):
                if m[0] == SIMPLE and len(m) >= 12 and m[1] == JOINGAME and \
                        struct.unpack_from("<I", m, 4)[0] == self.slot:
                    return True
        return False

    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)


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
a, b, c = clients

ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


def place(bx):
    """reports the positions, then lets the host set everyone on the ground"""
    for _ in range(5):
        b.send(position(bx, 60, 100))
        c.send(position(5, 60, 350))
        a.send(position(1, 60, 100))
        time.sleep(0.03)
        for cl in clients:
            cl.drain()
    for _ in range(10):
        time.sleep(0.03)
        for cl in clients:
            cl.drain()


def talk(first, count, extra=()):
    # voice only: a position sent along would put A back at y=60 for this
    # frame, while B already stands on the ground (maybe 100 units higher)
    for k in range(count):
        a.send(voice(c.slot, first + k, bytes([0x40 + k]) * 40), *extra)
        time.sleep(0.02)
        for cl in clients:
            cl.drain()


for cl in clients:
    cl.pump(0.3)
    cl.voices = []

# near (across the world edge) and far
place(1)
talk(100, 10)
for cl in clients:
    cl.pump(0.5)
check(len(b.voices) >= 8, "near player got %d/10 frames" % len(b.voices))
check(all(p == a.slot for p, s, f in b.voices), "relayed as the sender's slot %d (not the spoofed %d)" % (a.slot, c.slot))
check(all(f == bytes([0x40 + s - 100]) * 40 for p, s, f in b.voices), "frames intact, sequence kept")
check(not c.voices, "far player got nothing (%d)" % len(c.voices))
check(not a.voices, "sender doesn't get its own voice back (%d)" % len(a.voices))

# malformed VOICE: truncated, oversized, empty, length past the end
for cl in clients:
    cl.voices = []
bad = [
    bytes([VOICE, 0, 1]),
    struct.pack("<BBHH", VOICE, 0, 2, 401) + b"X" * 401,
    struct.pack("<BBHH", VOICE, 0, 3, 0),
    struct.pack("<BBHH", VOICE, 0, 4, 300) + b"Y" * 20,
    struct.pack("<BBHH", VOICE, 0, 5, 20) + b"Z" * 30,
]
for m in bad:
    a.send(position(1, 60, 100), m)
    time.sleep(0.02)
for cl in clients:
    cl.pump(0.4)
check(not b.voices and not c.voices, "malformed VOICE never relayed (%d, %d)" % (len(b.voices), len(c.voices)))

# B walks away: out of range
place(200)
talk(200, 10)
for cl in clients:
    cl.pump(0.5)
check(not b.voices, "player out of range got nothing (%d)" % len(b.voices))

# back in range
place(1)
b.voices = []
talk(300, 5)
for cl in clients:
    cl.pump(0.5)
check(len(b.voices) >= 4, "back in range: %d/5 frames" % len(b.voices))

# garbage opus frames next to the host, who plays them
a.pump(0.5)
if a.hostpos:
    hx, hy, hz = a.hostpos
    for k in range(30):
        a.send(position(hx + 2, hy, hz), voice(0, 400 + k, os.urandom(1 + k * 13)))
        time.sleep(0.02)
    print("sent garbage frames next to the host at (%.0f, %.0f, %.0f)" % (hx, hy, hz))
else:
    print("host position unknown, garbage frames skipped")
time.sleep(0.5)

probe.sendto(packet(simple(REQUESTINFO)), SERVER)
probe.settimeout(2.0)
try:
    data, addr = probe.recvfrom(65536)
    alive = any(m and m[0] == INFO for m in messages(data))
except socket.timeout:
    alive = False
check(alive, "server still answering")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
