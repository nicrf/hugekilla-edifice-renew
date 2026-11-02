"""Movement decided by the server (docs/M16.md, phases 2 and 3):
    HK_ANTICHEAT_LOG=1 HK_AC_VARS="props=0 sv_authmove=1" HK_AUTOSTART=1 <Edifice binary> 2>server.log &
    python3 tests/net_authmove.py server.log
Two clients join a local server: one announces HELLOCAP_INPUTS (the second
capability byte of REQUESTENTER) and sends numbered input commands (INPUTCMD), one
is a 0.7 build that sends its positions (PLAYERPOSITION), in the same game.
 - The new client is told the server moves it (NETSYNCMODE 256), the old one isn't.
 - Dead, then asking to live: the server respawns it where it chooses; walking
   forward moves it, every command run (SNAPSHOT: lastcmd, its exact state).
 - The old client sees it move where the server has it (PLAYERPOSITION relayed),
   and the new one sees the old one where it says it is.
 - A position from the new client (a teleport) is ignored.
 - A speed hack: commands three times as fast as time: the server runs no more than
   time allows (and ac_cmdslack ticks), logs "anticheat: cmd", and the player goes
   no faster than walking.
 - Quiet for 2 s, then 2 s of commands at once (time banked): the server repeats the
   last one 6 ticks, then runs no more than ac_cmdslack of the burst.
The server log (its stderr) must show the dropped commands."""
import math, os, re, socket, struct, sys, time

SERVER = ("127.0.0.1", int(os.environ.get("HK_TEST_PORT", "27020")))
REQUESTENTER, INFO, PLAYERPOSITION, SIMPLE = 1, 2, 3, 5
INPUTCMD, SNAPSHOT = 21, 22
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING, PINGREPLY = 1, 2, 4, 8, 12, 13
NETSYNCMODE = 41
HELLOCAP_INPUTS = 256
MOVEKEY_UP, MOVECMD_ALIVE = 1, 1
SYNCSTATE_GROUND = 2
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
LANDGRIDSIZE = 512
CMDSLACK = 8      # ac_cmdslack
TICK = 1 / 60
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


def command(keys=0, alive=True, yaw=0):
    """MoveCmd (netsync.h): keys, padfwd, padstrafe, yaw, pitch, weapon, flags, msec"""
    return struct.pack("<BbbHhBBB", keys, 0, 0, yaw, 0, 3, MOVECMD_ALIVE if alive else 0, 17)


def inputcmd(first, cmds):
    return struct.pack("<BBII", INPUTCMD, len(cmds), first, 0) + b"".join(cmds)


def wrapdist(a, b):
    """horizontal distance, the short way round the repeating world"""
    d = [(b[i] - a[i] + LANDGRIDSIZE / 2) % LANDGRIDSIZE - LANDGRIDSIZE / 2 for i in (0, 2)]
    return math.hypot(*d)


class Client:
    def __init__(self, name):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.name = name
        self.lastid = 0
        self.got = []        # every message received
        self.pos = {}        # player -> (x, y, z, state), from relayed positions
        self.syncmode = None # NETSYNCMODE msg
        self.snap = None     # newest SNAPSHOT: (lastcmd, (x, y, z), (vx, vy, vz), flags, playstate)
        self.snaps = 0
        self.cmd = 1000      # our next command's number

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
        if m[0] == SNAPSHOT and len(m) >= 14:
            tick, base, lastcmd, hasself = struct.unpack_from("<IIIB", m, 1)
            if hasself and len(m) >= 14 + 44:
                f = struct.unpack_from("<10fBBh", m, 14)
                if self.snap is None or (lastcmd - self.snap[0]) & 0xFFFFFFFF < 0x80000000:
                    self.snap = (lastcmd, f[0:3], f[3:6], f[10], f[11])
                self.snaps += 1

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

    def commands(self, n, **kw):
        """n new commands in one INPUTCMD (with up to 3 before them again)"""
        c = command(**kw)
        first = max(self.cmd - 3, self.cmd + n - 4) if n < 4 else self.cmd
        cmds = [c] * (self.cmd + n - first)
        sent = 0
        while sent < len(cmds):   # INPUTCMD takes 4 at most
            k = min(4, len(cmds) - sent)
            self.send(inputcmd(first + sent, cmds[sent:sent + k]))
            sent += k
        self.cmd += n


clients = []


def pump(wait):
    end = time.time() + wait
    while time.time() < end:
        for cl in clients:
            cl.drain()
        time.sleep(0.005)


def play(cl, secs, rate=1, old=None, oldpos=None, **kw):
    """cl sends rate commands per tick of real time for secs (the old client its
    position every 70 ms meanwhile); returns the ticks of real time that went by"""
    start = time.time()
    ticks = 0
    nextold = 0
    while time.time() - start < secs:
        cl.commands(rate, **kw)
        ticks += 1
        if old and time.time() >= nextold:
            old.send(position(*oldpos))
            nextold = time.time() + 0.07
        pump(max(0, start + ticks * TICK - time.time()))
    return (time.time() - start) / TICK


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


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
check(len(caps) >= 2 and caps[1] == HELLOCAP_INPUTS >> 8, "INFO announces HELLOCAP_INPUTS in its second byte")

mover, old = Client(b"Mover"), Client(b"Oldie")
# caps 0, no cookie, caps2: HELLOCAP_INPUTS
if not mover.join(trailer + bytes([0, HELLOCAP_INPUTS >> 8])):
    print("FAIL: the new client could not join"); sys.exit(1)
clients.append(mover)
if not old.join(trailer, clients):
    print("FAIL: the old client could not join"); sys.exit(1)
clients.append(old)
print("new client in slot %d, old one in slot %d" % (mover.slot, old.slot))
pump(1.0)
check(mover.syncmode == HELLOCAP_INPUTS, "the new client is moved by the server (NETSYNCMODE %s)" % mover.syncmode)
check(old.syncmode is None, "the old client isn't told anything")

# dead a moment (it arrives so), then alive: the server puts it somewhere
oldpos = (700.0, 60.0, 700.0)
play(mover, 0.5, alive=False, old=old, oldpos=oldpos)
first = mover.snap
check(first is not None and first[4] == 11, "dead at first (SNAPSHOT playstate %s)" % (first and first[4]))
play(mover, 0.5, old=old, oldpos=oldpos)
spawn = mover.snap
check(spawn and spawn[4] == 10 and spawn[1][1] > 50, "respawned by the server, at %s" % (spawn and [round(v, 1) for v in spawn[1]]))
# falls to the ground
for k in range(80):
    play(mover, 0.1, old=old, oldpos=oldpos)
    if mover.snap[3] & SYNCSTATE_GROUND and abs(mover.snap[2][1]) < .05:
        break
check(mover.snap[3] & SYNCSTATE_GROUND and abs(mover.snap[2][1]) < .05,
      "landed at %s" % [round(v, 1) for v in mover.snap[1]])

# walking: every command run, it goes forward
a = mover.snap   # (just after the last command: the server isn't repeating it yet)
sent0 = mover.cmd
snaps0 = mover.snaps
ticks = play(mover, 3, keys=MOVEKEY_UP, old=old, oldpos=oldpos)
pump(0.3)   # quiet: the server plays the last command 6 ticks more
b = mover.snap
ran = (b[0] - a[0]) & 0xFFFFFFFF
walked = wrapdist(a[1], b[1])
speed = walked / (ran * TICK) if ran else 0
print("walked %.1f units in %d commands (%.0f ticks of time): %.1f units/s, %d states" % (
    walked, ran, ticks, speed, mover.snaps - snaps0))
check(b[0] == mover.cmd - 1 + 6 and ran >= mover.cmd - sent0,
      "every command run, the last one repeated 6 ticks (last %d, sent up to %d)" % (b[0], mover.cmd - 1))
check(walked > 3, "it walked forward (%.1f units)" % walked)
check(mover.snaps - snaps0 > 3 * 25, "states come at 30 Hz (%d in 3 s)" % (mover.snaps - snaps0))

# the old client sees it where the server has it, and the new one sees the old one
seen = old.pos.get(mover.slot)
check(seen and wrapdist(seen, b[1]) < 1.5, "the old client sees it there (%s)" % (seen and [round(v, 1) for v in seen[:3]]))
seenold = mover.pos.get(old.slot)
check(seenold and wrapdist(seenold, oldpos) < 1 and seenold[3] == 10,
      "it sees the old client where it says it is (%s)" % (seenold and [round(v, 1) for v in seenold[:3]]))

# a position of its own: ignored
here = mover.snap[1]
for k in range(5):
    mover.send(position(here[0] + 100, here[1], here[2] + 100))
    play(mover, 0.07, old=old, oldpos=oldpos)
pump(0.3)
check(wrapdist(mover.snap[1], here) < 2, "its PLAYERPOSITION teleport is ignored")
check(wrapdist(old.pos[mover.slot], here) < 3, "the old client doesn't see it either")

# ---- a speed hack: three commands per tick of time ----
logmark = os.path.getsize(LOG) if LOG else 0
a = mover.snap
sent0 = mover.cmd
ticks = play(mover, 3, rate=3, keys=MOVEKEY_UP, old=old, oldpos=oldpos)
pump(0.3)
b = mover.snap
fast = wrapdist(a[1], b[1]) / (ticks * TICK)
print("speed hack: %d commands sent in %.0f ticks of time, %.1f units/s" % (mover.cmd - sent0, ticks, fast))
# (the command numbers go up with what was sent: the distance and the log tell what ran)
check(fast < speed * 1.4 + .5, "no faster than walking (%.1f units/s against %.1f)" % (fast, speed))
if LOG:
    with open(LOG, "rb") as f:
        f.seek(logmark)
        text = f.read().decode("utf-8", "replace")
    check("anticheat: cmd:" in text, "the server logs the commands it dropped")

# ---- time banked: quiet 2 s, then 2 s of commands at once ----
play(mover, 1, keys=MOVEKEY_UP, old=old, oldpos=oldpos)   # at the pace of time again
last = mover.cmd - 1
pump(2.0)   # nothing sent (pings still answered)
quiet = mover.snap
repeated = (quiet[0] - last) & 0xFFFFFFFF
check(repeated == 6, "quiet, its last command repeated 6 ticks (%d after %d)" % (repeated, last))
logmark = os.path.getsize(LOG) if LOG else 0
mover.commands(120, keys=MOVEKEY_UP)
pump(0.5)
b = mover.snap
if LOG:
    # "anticheat: cmd: Mover (1) sends commands faster than time, <so far> dropped"
    with open(LOG, "rb") as f:
        text = f.read().decode("utf-8", "replace")
    counts = [int(m) for m in re.findall(r"anticheat: cmd: .*?, (\d+) dropped", text[:logmark])]
    after = [int(m) for m in re.findall(r"anticheat: cmd: .*?, (\d+) dropped", text[logmark:])]
    dropped = (max(after) if after else 0) - (max(counts) if counts else 0)
    # of the 32 the server queues at most, about ac_cmdslack + 1 run, 22 or so are
    # dropped (the log has room for 20 lines after the quiet)
    check(dropped >= 15,
          "the burst's commands beyond ac_cmdslack are dropped (%d logged)" % dropped)
went = wrapdist(quiet[1], b[1])
print("burst of 120 commands after 2 s: last run %d, %.2f units (walking: %.2f a tick)" % (b[0] - last, went, speed * TICK))
# ac_cmdslack + 1 ticks of the burst, one more as time goes, then quiet again: the
# last one 6 ticks (all of them would be the 32 the server queues at most, and more)
check(went < (CMDSLACK + 2 + 6) * TICK * speed * 1.3 + .3,
      "the burst moves it no further than ac_cmdslack ticks (%.2f units)" % went)

for cl in clients:
    cl.send(simple(3))   # EXIT
print("PASS" if ok else "FAILED")
sys.exit(0 if ok else 1)
