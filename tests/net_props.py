"""Rocks and trees that block (docs/PROPS.md), against a local server:
    HK_PROPS_LOG=1 HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=1" HK_AUTOSTART=1 <Edifice> 2>server.log &
    python3 tests/net_props.py server.log <Edifice> on
(and with props=0 and "off"). HK_TEST_PORT: the server's info port (27020).
With props on:
 - INFO announces HELLOCAP_PROPS; a client without it (an older build) is refused
   with VERSIONMISMATCH and the text, and gets no slot;
 - clients with it join and get PROPSMODE 1 with the server's props hash (its log);
 - a game client (HK_JOIN) builds the same props from the land: same count, same hash;
 - a bullet the server simulates stops on a trunk: no SERVERHIT for a victim behind
   the tree, while the same shot beside the tree hits it.
With props off: no HELLOCAP_PROPS in INFO, an older client joins, a new one gets
PROPSMODE 0."""
import math, os, re, shutil, socket, struct, subprocess, sys, tempfile, time

PORT = int(os.environ.get("HK_TEST_PORT", "27020"))
SERVER = ("127.0.0.1", PORT)
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE, CHATMESSAGE, FIREGUN = 1, 2, 3, 5, 8, 10
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
CHANGETEAM, VERSIONMISMATCH, SERVERHIT, PROPSMODE = 20, 26, 28, 30
HELLOCAP_SERVERHITS, HELLOCAP_PROPS = 8, 64
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
LOG = sys.argv[1]
GAME = sys.argv[2] if len(sys.argv) > 2 else None
MODE = sys.argv[3] if len(sys.argv) > 3 else "on"


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


def is_simple(m, t, msg=None, msg2=None):
    if m[0] != SIMPLE or len(m) < 12 or m[1] != t:
        return False
    a, b = struct.unpack_from("<II", m, 4)
    return (msg is None or a == msg) and (msg2 is None or b == msg2)


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.got = []
        self.pos = {}
        self.teams = {}

    def note(self, m):
        self.got.append(m)
        if m[0] == SIMPLE and len(m) >= 12:
            t, msg, msg2 = m[1], *struct.unpack_from("<II", m, 4)
            if t == CHANGETEAM:
                self.teams[msg] = msg2
            if t == PING:
                self.s.sendto(packet(simple(PINGREPLY, msg, 0)), self.client)
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

    def request(self, trailer):
        self.s.sendto(packet(bytes([REQUESTENTER, len(self.name)]) + self.name + b"\x00" + trailer), SERVER)

    def join(self, trailer, others=()):
        """True once in the game; the answers to REQUESTENTER are in self.answers"""
        self.request(trailer)
        self.answers = []
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
                    self.answers.append(m)
                    if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
                        p, self.slot = struct.unpack_from("<II", m, 4)
                        port = socket.ntohs(p & 0xFFFF)
                    if len(m) >= 12 and m[0] == SIMPLE and m[1] == VERSIONMISMATCH:
                        end = min(end, time.time() + 0.5)   # its text follows at once
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
            if any(is_simple(m, JOINGAME, self.slot) for m in self.got):
                return True
            time.sleep(0.05)
        return False

    def send(self, *msgs):
        self.s.sendto(packet(*msgs), self.client)


clients = []


def pump(wait):
    end = time.time() + wait
    while time.time() < end:
        for cl in clients:
            cl.drain()
        time.sleep(0.01)


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


def server_props():
    """(count, hash, [(kind, x, y, z, r, h)]) of the server's last land, from its log"""
    for _ in range(50):
        log = open(LOG, errors="replace").read()
        heads = [m for m in re.finditer(r"^props: (\d+) hash ([0-9a-f]{8})$", log, re.M)]
        if heads:
            last = heads[-1]
            lines = re.findall(r"^prop \d+ (\d) (\S+) (\S+) (\S+) (\S+) (\S+)$", log[last.end():], re.M)
            props = [(int(k), *map(float, rest)) for k, *rest in lines][:int(last.group(1))]
            return int(last.group(1)), int(last.group(2), 16), props
        time.sleep(0.2)
    return None


# ---- INFO ----
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
newcaps = bytes([HELLOCAP_SERVERHITS | HELLOCAP_PROPS])
sp = server_props()
if not sp:
    print("FAIL: no 'props:' line in the server log (HK_PROPS_LOG=1)"); sys.exit(1)
count, phash, props = sp
print("server: %d rocks and trees, hash %08x, INFO caps %d" % (count, phash, caps))

if MODE == "off":
    check(not caps & HELLOCAP_PROPS, "INFO does not announce rocks and trees")
    old = Client(b"Old")
    check(old.join(trailer), "a client without them (older build) joins")
    clients.append(old)
    new = Client(b"New")
    check(new.join(trailer + newcaps, clients), "a client with them joins")
    clients.append(new)
    pump(1.0)
    check(any(is_simple(m, PROPSMODE, 0) for m in new.got), "and is told there are none (PROPSMODE 0)")
    check(not any(is_simple(m, PROPSMODE) for m in old.got), "the older client never gets PROPSMODE")
    print("ALL OK" if ok else "SOME CHECKS FAILED")
    sys.exit(0 if ok else 1)

check(caps & HELLOCAP_PROPS, "INFO announces rocks and trees (HELLOCAP_PROPS)")
check(count > 50 and len(props) == count, "the server has %d of them, all logged" % count)

# ---- an older build is refused, with the reason ----
old = Client(b"Old")
check(not old.join(trailer), "a client without HELLOCAP_PROPS gets no slot")
refused = [m for m in old.answers if is_simple(m, VERSIONMISMATCH)]
text = b"".join(m for m in old.answers if m[0] == CHATMESSAGE)
check(refused and struct.unpack_from("<I", refused[0], 4)[0] == 5, "it is refused (VERSIONMISMATCH, protocol 5)")
check(b"rocks and trees" in text, "with the text %r" % text[5:].decode(errors="replace"))

# ---- new clients: PROPSMODE with the server's hash ----
for n in (b"Alice", b"Bob", b"Carol"):
    c = Client(n)
    if not c.join(trailer + newcaps, clients):
        print("FAIL: %s could not join" % n.decode()); sys.exit(1)
    clients.append(c)
    print("%s joined as slot %d" % (n.decode(), c.slot))
pump(1.0)
for c in clients:
    modes = [struct.unpack_from("<II", m, 4) for m in c.got if is_simple(m, PROPSMODE)]
    check(modes and modes[-1] == (1, phash), "%s: PROPSMODE 1, hash %s" % (
        c.name.decode(), "%08x" % modes[-1][1] if modes else "none"))

# ---- a game client builds the same ones ----
if GAME:
    d = tempfile.mkdtemp(prefix="edifice-props-")
    for f in ("control", "land", "net", "object", "skies", "sv"):
        shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", f + ".ini"), d)
    lines = open(os.path.join(d, "net.ini"), "rb").read().decode().split("\r\n")
    fix = {"infosendport": str(PORT), "datasendport": str(PORT + 1), "lobby": "", "servervisibility": "lan"}
    lines = [k + "=" + fix[k] if k in fix else l for l, k in ((l, l.split("=")[0]) for l in lines)]
    open(os.path.join(d, "net.ini"), "wb").write("\r\n".join(lines).encode())
    clog = os.path.join(os.path.dirname(os.path.abspath(LOG)), "net_props-client.log")
    env = dict(os.environ, HK_USERDIR=d + "/", HK_JOIN="127.0.0.1", HK_PROPS_LOG="1")
    env.pop("HK_AUTOSTART", None)
    env.pop("HK_AC_VARS", None)
    proc = subprocess.Popen([GAME], env=env, stdout=open(clog, "wb"), stderr=subprocess.STDOUT)
    same = None
    end = time.time() + 60
    while time.time() < end and same is None:
        pump(0.5)
        c = open(clog, errors="replace").read()
        m = re.search(r"rocks and trees: (\d+), same as the server's \(([0-9a-f]{8})\)", c)
        if m:
            same = (int(m.group(1)), int(m.group(2), 16))
        elif "rocks and trees differ" in c:
            same = False
    proc.kill()
    proc.wait()
    shutil.rmtree(d, ignore_errors=True)
    check(same and same == (count, phash), "a game client (HK_JOIN) built the same %s rocks and trees" % (
        same[0] if same else "?"))
    clients_props = re.findall(r"^prop \d+ .*$", open(clog, errors="replace").read(), re.M)
    serverlines = re.findall(r"^prop \d+ .*$", open(LOG, errors="replace").read(), re.M)[-count:]
    check(clients_props[-count:] == serverlines, "the same %d positions, sizes, heights" % count)
else:
    print("no game binary given: the game client's props not checked")

# ---- a bullet stops on a trunk ----
teams = {}
for cl in clients:
    teams.update(cl.teams)
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
eye = next(c for c in clients if c is not victim and c is not shooter)
print("victim %d, shooter %d, watcher %d" % (victim.slot, shooter.slot, eye.slot))


def place(vp, sp):
    """victim at vp, shooter at sp (x, z), dropped there; where the server has them"""
    for _ in range(4):
        victim.send(position(vp[0], 120, vp[1]))
        shooter.send(position(sp[0], 120, sp[1]))
        eye.send(position(vp[0] + 60, 120, vp[1] + 60))
        pump(0.05)
    pump(2.5)   # the host lets them fall to the ground
    return eye.pos.get(victim.slot), eye.pos.get(shooter.slot)


def volley(v, g, n=12):
    """n shots from the shooter's place at the victim's body: SERVERHITs got"""
    mark = len(victim.got)
    origin = (g[0], g[1], g[2])
    d = (v[0] - g[0], (v[1] - 1) - (g[1] - 0.2), v[2] - g[2])
    k = math.sqrt(sum(c * c for c in d))
    d = tuple(c / k for c in d)
    for _ in range(n):
        shooter.send(position(g[0], g[1], g[2]), firegun(origin, d))
        victim.send(position(v[0], v[1], v[2]))
        pump(0.15)
    pump(0.6)
    return sum(1 for m in victim.got[mark:] if is_simple(m, SERVERHIT))


def lonely(p):
    return all(math.hypot(q[1] - p[1], q[3] - p[3]) > 12 for q in props if q is not p)


trees = [p for p in props if p[0] in (0, 1) and lonely(p)]
print("%d lone trees to try" % len(trees))
done = False
for p in trees[:6]:
    _, tx, ty, tz, tr, th = p
    # behind the trunk 3 units on x, the shooter 6 units in front
    v, g = place((tx + 3, tz), (tx - 6, tz))
    if not v or not g or abs(v[2] - tz) > 0.5 or abs(g[2] - tz) > 0.5:
        continue
    shooter.send(firegun(g[:3], (0, 1, 0), reload=1))   # a full magazine
    pump(2.6)
    blocked = volley(v, g)
    # the same shots 4 units beside the tree: the line of sight was good
    v2, g2 = place((tx + 3, tz + 4), (tx - 6, tz + 4))
    if not v2 or not g2:
        continue
    shooter.send(firegun(g2[:3], (0, 1, 0), reload=1))
    pump(2.6)
    beside = volley(v2, g2, 4)
    print("tree at %.1f %.1f (radius %.2f, %.1f high): %d SERVERHIT through it, %d beside it" % (
        tx, tz, tr, th, blocked, beside))
    if beside:
        check(blocked == 0, "shots through a trunk do no damage (%d SERVERHIT)" % blocked)
        done = True
        break
check(done, "found a tree with a clear shot beside it")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
