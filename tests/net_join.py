"""Joins a running local server like a real client and decodes what it sends
(roadmap M6, explicit wire format: see the Sync* functions in src/net/multiplay.cpp):
    HK_AUTOSTART=1 <Edifice binary> &  then  python3 tests/net_join.py
REQUESTENTER -> ENTER, reliable PORTACK on the per-client port, then PLAYERNAMES,
LANDINFO, BLOCKS, SERVEROBJECTSNAPSHOT, SERVERVARS, REMOTEGAMEDATA and JOINGAME,
each acknowledged like the game does. Checks sizes and decoded values, then sends
truncated versions of the parsed messages and checks the server survives.
NET_JOIN_DUMP=<dir> also saves every message and datagram received there
(seeds for tests/fuzz, and what tests/net_map.py checks)."""
import math, os, socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
REQUESTENTER, INFO, PLAYERPOSITION, LANDINFO, BLOCKS, CHATMESSAGE = 1, 2, 3, 4, 7, 8
FIREGUN, SERVERVARS, UPDATESERVEROBJECT, SNAPSHOT, KILLEDPOSITION, RGD = 10, 11, 12, 13, 14, 15
PLAYERNAMES, SIMPLE = 6, 5
REQUESTINFO, ENTER, PORTACK, JOINGAME, PING = 1, 2, 4, 8, 12
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
MAGIC = 0x31564445
NUMDEF, MAXSERVEROBJS = 160, 100
LANDINFOSIZE = 1 + 4 + NUMDEF * 29 + 9 * 60 + 16 + 17 * 4 + 3 * 60 + 4
SERVERVARSSIZE = 1 + 46 * 4
SNAPSHOTSIZE = 1 + MAXSERVEROBJS * 43


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


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))


def recv(wait):
    s.settimeout(wait)
    try:
        return s.recvfrom(65536)
    except socket.timeout:
        return None, None


# version trailer from INFO
info = None
for _ in range(40):
    s.sendto(packet(simple(REQUESTINFO)), SERVER)
    end = time.time() + 1.0
    while time.time() < end and not info:
        data, addr = recv(0.2)
        if data and data[0] == 0:
            info = next((m for m in messages(data) if m and m[0] == INFO), None)
    if info:
        break
if not info:
    print("FAIL: no INFO reply"); sys.exit(1)
t = info[2 + info[1] + 1:]
_, proto, layout, vlen = struct.unpack_from("<IHIB", t, 0)
trailer = t[:11 + vlen]

name = b"Joiner"
s.sendto(packet(bytes([REQUESTENTER, len(name)]) + name + b"\x00" + trailer), SERVER)
port = slot = None
end = time.time() + 3
while time.time() < end and port is None:
    data, addr = recv(0.5)
    if data and data[0] == 0:
        for m in messages(data):
            if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
                p, slot = struct.unpack_from("<II", m, 4)
                port = socket.ntohs(p & 0xFFFF)
if port is None:
    print("FAIL: no ENTER"); sys.exit(1)
client = (SERVER[0], port)
print("ENTER: slot %d, port %d" % (slot, port))

# reliable PORTACK (first transfer: id 1, part 0, uint32 size then data)
payload = simple(PORTACK, socket.htons(port))
body = struct.pack("<HHI", 1, 0, len(payload)) + payload
s.sendto(bytes([LASTRELIABLE]) + checksum(body) + body, client)

got = {}            # type -> list of messages
raw = []            # datagrams, as received
parts = {}
lastid = 0
end = time.time() + 15
while time.time() < end and not any(len(m) >= 12 and m[1] == JOINGAME and
                                     struct.unpack_from("<I", m, 4)[0] == slot for m in got.get(SIMPLE, [])):
    data, addr = recv(0.5)
    if not data:
        continue
    raw.append(data)
    kind = data[0]
    if kind in (RELIABLE, LASTRELIABLE) and len(data) >= 9:
        body = data[5:]
        if checksum(body) != data[1:5]:
            print("FAIL: bad reliable checksum"); sys.exit(1)
        rid, num = struct.unpack_from("<HH", body, 0)
        s.sendto(bytes([ACK]) + body[:4], client)
        if rid != (lastid + 1) & 0xFFFF:
            continue   # resent part of a finished transfer
        parts[num] = body[4:]
        if kind == LASTRELIABLE:
            lastid = rid
            buf = b"".join(parts[i] for i in sorted(parts))
            parts = {}
            pos = 0
            while pos + 4 <= len(buf):
                (n,) = struct.unpack_from("<I", buf, pos)
                m = buf[pos + 4:pos + 4 + n]
                got.setdefault(m[0], []).append(m)
                pos += 4 + n
    elif kind == 0:
        for m in messages(data):
            if m:
                got.setdefault(m[0], []).append(m)
                if m[0] == SIMPLE and len(m) >= 12 and m[1] == PING:
                    (pid,) = struct.unpack_from("<I", m, 4)
                    s.sendto(packet(simple(13, pid, 0)), client)

dump = os.environ.get("NET_JOIN_DUMP")
if dump:
    os.makedirs(dump, exist_ok=True)
    for t, msgs in got.items():
        for i, m in enumerate(msgs):
            with open(os.path.join(dump, "msg-%d-%d.bin" % (t, i)), "wb") as f:
                f.write(m)
    for i, d in enumerate(raw):
        with open(os.path.join(dump, "datagram-%d.bin" % i), "wb") as f:
            f.write(d)

ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


names = got.get(PLAYERNAMES, [])
players = {}
for m in names:
    count, pos = m[1], 2
    for _ in range(count):
        num, kills, deaths, team, nl = struct.unpack_from("<BiibB", m, pos)
        players[num] = m[pos + 11:pos + 11 + nl].decode(errors="replace")
        pos += 11 + nl
check(players.get(slot) == "Joiner" and 0 in players, "PLAYERNAMES %r" % players)

land = got.get(LANDINFO, [])
check(land and len(land[0]) == LANDINFOSIZE, "LANDINFO size %s (expected %d)" % (len(land[0]) if land else None, LANDINFOSIZE))
if land:
    m = land[0]
    (numdefs,) = struct.unpack_from("<i", m, 1)
    pos = 5 + NUMDEF * 29
    tex = [m[pos + 60 * i:pos + 60 * (i + 1)].split(b"\0")[0].decode(errors="replace") for i in range(9)]
    numgroundtex, seed, numscenery, numblotch = struct.unpack_from("<iIII", m, pos + 540)
    sky = struct.unpack_from("<11f4i2f", m, pos + 540 + 16)
    cloud = m[pos + 540 + 16 + 68:pos + 540 + 16 + 68 + 60].split(b"\0")[0].decode(errors="replace")
    check(0 <= numdefs < NUMDEF and 0 < numgroundtex <= 9 and all(tex[:numgroundtex]),
          "LANDINFO defs=%d groundtex=%s seed=%u scenery=%d blotch=%d" % (numdefs, tex[:numgroundtex], seed, numscenery, numblotch))
    check(all(math.isfinite(x) for x in sky[:11]), "LANDINFO sky sunheight=%.2f fog=(%.2f %.2f %.2f) clouds=%r" % (sky[0], sky[1], sky[2], sky[3], cloud))

blocks = got.get(BLOCKS, [])
check(blocks and all(len(b) >= 5 and (len(b) - 5) // 9 >= struct.unpack_from("<I", b, 1)[0] for b in blocks),
      "BLOCKS: %d message(s)" % len(blocks))
snap = got.get(SNAPSHOT, [])
check(snap and len(snap[0]) == SNAPSHOTSIZE, "SERVEROBJECTSNAPSHOT size %s (expected %d)" % (len(snap[0]) if snap else None, SNAPSHOTSIZE))
sv = got.get(SERVERVARS, [])
check(sv and len(sv[0]) == SERVERVARSSIZE, "SERVERVARS size %s (expected %d)" % (len(sv[0]) if sv else None, SERVERVARSSIZE))
if sv:
    v = struct.unpack_from("<5f2i7i1i7i7i7f6f4i", sv[0], 1)
    gravity, gamemode, resend = v[4], v[-2], v[-1]
    check(math.isfinite(gravity) and gravity != 0 and 0 < resend < 100000,
          "SERVERVARS gravity=%.4f gamemode=%d positionresendtime=%d" % (gravity, gamemode, resend))
rgd = got.get(RGD, [])
check(rgd and len(rgd[0]) == 5, "REMOTEGAMEDATA %s" % (rgd[0].hex() if rgd else None))
joined = [m for m in got.get(SIMPLE, []) if len(m) >= 12 and m[1] == JOINGAME]
check(any(struct.unpack_from("<I", m, 4)[0] == slot for m in joined), "JOINGAME for slot %d" % slot)

# truncated versions of every parsed message, on the per-client port
for t in (SIMPLE, PLAYERPOSITION, FIREGUN, KILLEDPOSITION, CHATMESSAGE, BLOCKS):
    for n in range(0, 8):
        s.sendto(packet(bytes([t]) + b"\x01" * n), client)
s.sendto(packet(bytes([REQUESTENTER, 255]) + b"X" * 10), SERVER)   # name length past the end
s.sendto(packet(bytes([REQUESTENTER])), SERVER)
time.sleep(1.0)
s.sendto(packet(simple(REQUESTINFO)), SERVER)
alive = False
end = time.time() + 3
while time.time() < end and not alive:
    data, addr = recv(0.5)
    alive = bool(data) and data[0] == 0 and any(m and m[0] == INFO for m in messages(data))
check(alive, "server still answering after truncated messages")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
