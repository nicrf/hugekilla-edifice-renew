"""A test client of protocol 6 (docs/M16.md) for the tests/net_*.py scripts: the
handshake, the reliable transfers, the input commands (INPUTCMD, acking the newest
SNAPSHOT), the snapshots (our exact state, the others as deltas), TICKEVENT, shots
with their tick (FIREGUN + FireTick). Since protocol 6 a client can't say where it
is: the server moves it from its commands; a server started with HK_TESTPLACE=1 puts
it where it asks with a chat line (Client.place, tests only).
HK_TEST_PORT: the server's info port if not 27020."""
import math, os, socket, struct, sys, time

SERVER = ("127.0.0.1", int(os.environ.get("HK_TEST_PORT", "27020")))
REQUESTENTER, INFO, SIMPLE, BLOCKS, CHATMESSAGE, FIREGUN = 1, 2, 5, 7, 8, 10
UPDATESERVEROBJECT, SERVEROBJECTSNAPSHOT, KILLEDPOSITION, VOICE, THROWSMOKE = 12, 13, 14, 16, 20
INPUTCMD, SNAPSHOT, TICKEVENT = 21, 22, 23
REQUESTINFO, ENTER, EXIT, PORTACK, JOINGAME, DELETEPLAYER, PING, PINGREPLY = 1, 2, 3, 4, 8, 11, 12, 13
PAIN, KILLED, CHANGETEAM, VERSIONMISMATCH, SERVERHIT, HITMODE, PROPSMODE = 18, 19, 20, 26, 28, 29, 30
SMOKEMODE, NETSYNCMODE = 40, 41
PLAYERNAMES = 6
HELLOCAP_COOKIE, HELLOCAP_SERVERHITS, HELLOCAP_STABLESLOTS, HELLOCAP_PROPS, HELLOCAP_SMOKE = 1, 8, 16, 64, 128
TE_KILLED, TE_PAIN, TE_GRENADE, TE_EXPLODE, TE_SMOKE = 1, 2, 3, 4, 5
MOVEKEY_UP, MOVEKEY_DOWN, MOVEKEY_LEFT, MOVEKEY_RIGHT, MOVEKEY_JETPACK = 1, 2, 4, 8, 16
MOVECMD_ALIVE = 1
PLAYERLEFT_KEEPSLOTS = 0x100
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
SNAPRING = 32
TICK = 1 / 60
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
    return struct.pack("<bbxxII", SIMPLE, type2, msg & 0xFFFFFFFF, msg2 & 0xFFFFFFFF)


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


def is_simple(m, t):
    return m and m[0] == SIMPLE and len(m) >= 12 and m[1] == t


def simple_args(m):
    return struct.unpack_from("<II", m, 4)


def newer(a, b):
    return 0 < (a - b) & 0xFFFFFFFF < 0x80000000


def wrapdist(a, b):
    d = [(b[i] - a[i] + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2 for i in (0, 2)]
    return math.hypot(d[0], d[1], b[1] - a[1])


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


def aim(origin, target):
    """the direction from a player's position to another's body (the bullet starts .2
    below the shot's position, HandleFireGun; a player's body is the 2 units under it)"""
    w = lambda v: (v + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2   # the world repeats
    d = (w(target[0] - origin[0]), (target[1] - 1) - (origin[1] - 0.2), w(target[2] - origin[2]))
    n = math.sqrt(sum(c * c for c in d)) or 1
    return tuple(c / n for c in d)


def tickevent(m):
    """the fields of a TICKEVENT (netsync.h)"""
    tick, kind = struct.unpack_from("<IB", m, 1)
    e = dict(tick=tick, kind=kind)
    if kind == TE_KILLED:
        e.update(killed=m[6], killer=m[7], haspos=m[8])
        if m[8]:
            e.update(pos=struct.unpack_from("<3f", m, 9))
    elif kind == TE_PAIN:
        e.update(player=m[6], sound=struct.unpack_from("<H", m, 7)[0])
    elif kind in (TE_GRENADE, TE_EXPLODE):
        e.update(player=m[6], objid=struct.unpack_from("<H", m, 7)[0], pos=struct.unpack_from("<3f", m, 9))
        if kind == TE_GRENADE:
            e.update(vel=struct.unpack_from("<3f", m, 21), ms=struct.unpack_from("<H", m, 33)[0])
    elif kind == TE_SMOKE:
        e.update(player=m[6], objid=struct.unpack_from("<H", m, 7)[0], slot=m[9])
    return e


def server_info(timeout=40):
    """(trailer, INFO caps byte, INFO message) of the local server; exits if it doesn't answer"""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    for _ in range(timeout):
        probe.sendto(packet(simple(REQUESTINFO)), SERVER)
        probe.settimeout(1.0)
        try:
            data, addr = probe.recvfrom(65536)
            info = next((m for m in messages(data) if m and m[0] == INFO), None)
        except socket.timeout:
            info = None
        if info:
            t = info[2 + info[1] + 1:]
            vlen = t[10]
            caps = t[11 + vlen + 4] if len(t) > 11 + vlen + 4 else 0
            return t[:11 + vlen], caps, info
    print("FAIL: no INFO reply")
    sys.exit(1)


class Client:
    """caps: the capability byte of its REQUESTENTER (this build's by default)"""
    def __init__(self, name, caps=HELLOCAP_SERVERHITS | HELLOCAP_STABLESLOTS | HELLOCAP_SMOKE):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name if isinstance(name, bytes) else name.encode()
        self.caps = caps
        self.lastid = 0
        self.got = []         # every message, in order
        self.at = []          # when each came
        self.syncmode = None  # NETSYNCMODE came (the server's tick)
        self.teams = {}
        self.recv = {}        # tick -> {pnum: fields}: the snapshots kept (complete)
        self.newest = None    # newest tick: what we ack
        self.own = None       # our exact state (x, y, z, vx, vy, vz)
        self.ownstate = None  # our playstate, life
        self.lastcmd = None   # our last command the server ran
        self.cmd = 1000
        self.keys, self.weapon, self.alive, self.yaw, self.pitch = 0, 3, True, 0, 0
        self.slot = None

    # ---- what comes in ----
    def snapshot(self, m):
        tick, base, lastcmd, hasself = struct.unpack_from("<IIIB", m, 1)
        pos = 14
        if hasself:
            f = struct.unpack_from("<10fBBh", m, pos)
            if self.newest is None or newer(tick, self.newest):
                self.own = f[0:6]
                self.ownstate = (f[11], f[12])
                self.lastcmd = lastcmd
            pos += 44
        n = m[pos]
        pos += 1
        if base and base not in self.recv:
            return
        players = {k: dict(v) for k, v in self.recv[base].items()} if base else {}
        for _ in range(n):
            pnum, mask = m[pos], m[pos + 1]
            pos += 2
            if mask == F_GONE:
                players.pop(pnum, None)
                continue
            p = players.setdefault(pnum, {})
            for bit, fmt, names in FIELDS:
                if mask & bit:
                    p.update(zip(names, struct.unpack_from(fmt, m, pos)))
                    pos += struct.calcsize(fmt)
        self.recv[tick] = players
        if len(self.recv) > SNAPRING:
            del self.recv[min(self.recv, key=lambda t: (t - tick + (1 << 31)) & 0xFFFFFFFF)]
        if self.newest is None or newer(tick, self.newest):
            self.newest = tick

    def note(self, m):
        self.got.append(m)
        self.at.append(time.time())
        if m[0] == SIMPLE and len(m) >= 12:
            t, (msg, msg2) = m[1], simple_args(m)
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
            if t == NETSYNCMODE:
                self.syncmode = msg2
            if t == CHANGETEAM:
                self.teams[msg] = msg2
            if t == DELETEPLAYER:
                # what a client does: renumbered unless the server keeps the slots; the
                # snapshots name the players by number: forgotten (complete ones next)
                if not msg2 & PLAYERLEFT_KEEPSLOTS:
                    if self.slot is not None and msg < self.slot:
                        self.slot -= 1
                    self.teams = {(p - 1 if p > msg else p): t for p, t in self.teams.items() if p != msg}
                    self.recv, self.newest = {}, None
                else:
                    self.teams.pop(msg, None)
        if m[0] == PLAYERNAMES and len(m) >= 2:
            for num, kills, deaths, team, name in player_names(m):
                self.teams[num] = team
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

    # ---- joining ----
    def request(self, trailer, extra=None):
        caps = bytes([self.caps]) if extra is None else extra
        self.s.sendto(packet(bytes([REQUESTENTER, len(self.name)]) + self.name + b"\x00" + trailer + caps), SERVER)

    def join(self, trailer, others=(), extra=None):
        """REQUESTENTER (trailer + our caps, or extra), then everything up to JOINGAME and
        NETSYNCMODE; 0 if the server doesn't take us"""
        self.request(trailer, extra)
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
                    self.got.append(m)
                    if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
                        p, self.slot = simple_args(m)
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
            if self.syncmode is not None and any(is_simple(m, JOINGAME) and simple_args(m)[0] == self.slot
                                                 for m in self.got):
                return True
            time.sleep(0.02)
        return False

    # ---- what goes out ----
    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)

    def command(self):
        return struct.pack("<BbbHhBBB", self.keys, 0, 0, self.yaw & 0xFFFF, self.pitch, self.weapon,
                           MOVECMD_ALIVE if self.alive else 0, 17)

    def tick(self):
        """this tick's command (with the three before it), acking the newest snapshot"""
        c = self.command()
        self.send(struct.pack("<BBII", INPUTCMD, 4, self.cmd - 3, self.newest or 0) + c * 4)
        self.cmd += 1

    def place(self, x, y, z):
        """put our player there (the server's HK_TESTPLACE)"""
        text = b"/place %.3f %.3f %.3f" % (x, y, z)
        self.send(bytes([CHATMESSAGE, 0, 0]) + struct.pack("<H", len(text)) + text)

    def shot(self, d, pos=None, reload=0, objid=0, kind=FIREGUN, viewback=6):
        """FIREGUN (or THROWSMOKE) along d, + FireTick: we saw the others viewback ticks
        before our newest snapshot, after our last command sent"""
        pos = pos or (self.own[0:3] if self.own else (0, 0, 0))
        view = (self.newest - viewback) & 0xFFFFFFFF if self.newest else 0
        return struct.pack("<bbb3f3f", kind, 0, reload, d[0], d[1], d[2], pos[0], pos[1], pos[2]) + \
            struct.pack("<IBIH", view, 0, (self.cmd - 1) & 0xFFFFFFFF, objid)

    def fire(self, d, **kw):
        self.send(self.shot(d, **kw))

    def leave(self):
        for _ in range(3):
            self.send(simple(EXIT))
        self.s.close()
        del self.client

    # ---- what we know ----
    def others(self):
        """{pnum: (x, y, z, state)} in the newest snapshot"""
        if self.newest is None or self.newest not in self.recv:
            return {}
        return {p: (f["x"] / 128, f["y"] / 64, f["z"] / 128, f["state"]) for p, f in self.recv[self.newest].items()}

    def mark(self):
        return len(self.got)

    def since(self, mark):
        return self.got[mark:]

    def events(self, mark, kind=None):
        return [tickevent(m) for m in self.got[mark:] if m[0] == TICKEVENT and len(m) >= 6 and
                (kind is None or m[5] == kind)]

    def simples(self, mark, t):
        return [simple_args(m) for m in self.got[mark:] if is_simple(m, t)]


class Game:
    """the clients of a test, each sending its command every tick while pumped"""
    def __init__(self):
        self.clients = []
        self.ok = True

    def add(self, c):
        self.clients.append(c)

    def check(self, cond, what):
        print(("ok   " if cond else "FAIL ") + what, flush=True)
        self.ok &= bool(cond)
        return cond

    def pump(self, secs, playing=None, each=None):
        """secs of play: the clients in playing (all that joined by default) send a
        command a tick; each(k) is called every tick"""
        playing = [c for c in self.clients if hasattr(c, "client")] if playing is None else playing
        start = time.time()
        k = 0
        while time.time() - start < secs:
            for c in playing:
                c.tick()
            if each:
                each(k)
            k += 1
            end = start + k * TICK
            while True:
                for c in self.clients:
                    c.drain()
                if time.time() >= end:
                    break
                time.sleep(0.002)

    def settle(self, clients, secs=4.0, still=0.02):
        """alive, until the server's state of each one stops moving (fallen to the ground),
        secs at most"""
        end = time.time() + secs
        last = {}
        while time.time() < end:
            self.pump(0.25)
            now = {c.name: c.own for c in clients}
            if all(c.own and c.name in last and last[c.name] and
                   wrapdist(last[c.name][0:3], c.own[0:3]) < still for c in clients):
                return True
            last = now
        return False

    def finish(self, log=None):
        for c in self.clients:
            if hasattr(c, "client"):
                c.send(simple(EXIT))
        if log:
            text = open(log, "rb").read().decode("utf-8", "replace")
            events = [l for l in text.splitlines() if l.startswith("anticheat: ")]
            self.check(not events, "no anti-cheat event (%s)" % (events[:3] or "none"))
        print("PASS" if self.ok else "FAILED")
        sys.exit(0 if self.ok else 1)
