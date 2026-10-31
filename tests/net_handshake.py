"""Checks the netplay version handshake against a running local server:
    HK_AUTOSTART=1 <Edifice binary> &  then  python3 tests/net_handshake.py
(server info port 27020, see data/net.ini)."""
import socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
SIMPLE, INFO, REQUESTENTER, CHATMESSAGE = 5, 2, 1, 8
REQUESTINFO, ENTER, SERVERFULL, VERSIONMISMATCH = 1, 2, 15, 26
MAGIC = 0x31564445


def packet(*msgs):
    # Sock framing: 1 byte type (0 = plain), then (uint16 size, payload)*
    out = b"\x00"
    for m in msgs:
        out += struct.pack("<H", len(m)) + m
    return out


def simple(type2, msg=0, msg2=0):
    return struct.pack("<bbxxII", SIMPLE, type2, msg, msg2)   # sizeof(SimpleMessage) == 12


def messages(data):
    if not data or data[0] != 0:
        return []
    pos, out = 1, []
    while pos + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, pos)
        out.append(data[pos + 2:pos + 2 + n])
        pos += 2 + n
    return out


def trailer(layout, proto=2, ver=b"0.2.0"):
    return struct.pack("<IHIB", MAGIC, proto, layout, len(ver)) + ver


def recv_all(s, wait=1.5):
    s.settimeout(wait)
    got = []
    end = time.time() + wait
    while time.time() < end:
        try:
            data, _ = s.recvfrom(65536)
        except socket.timeout:
            break
        got += messages(data)
    return got


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))
ok = True

# 1. INFO carries the version trailer (retry while the server is still loading)
infos = []
for _ in range(40):
    s.sendto(packet(simple(REQUESTINFO)), SERVER)
    infos = [m for m in recv_all(s, 1.0) if m and m[0] == INFO]
    if infos:
        break
if not infos:
    print("FAIL: no INFO reply"); sys.exit(1)
m = infos[0]
namelen = m[1]
name = m[2:2 + namelen].decode()
t = m[2 + namelen + 1:]
magic, proto, layout, vlen = struct.unpack_from("<IHIB", t, 0)
ver = t[11:11 + vlen].decode()
print("INFO: name=%r magic_ok=%s protocol=%d layout=%08x version=%s" % (name, magic == MAGIC, proto, layout, ver))
ok &= magic == MAGIC and proto == 2

# 2. original client (no trailer) is refused with VERSIONMISMATCH + readable text
s.sendto(packet(bytes([REQUESTENTER, 3]) + b"Old\x00"), SERVER)
got = recv_all(s)
mismatch = [g for g in got if len(g) >= 2 and g[0] == SIMPLE and g[1] == VERSIONMISMATCH]
chats = [g for g in got if g and g[0] == CHATMESSAGE]
print("legacy REQUESTENTER ->", "VERSIONMISMATCH" if mismatch else "no refusal",
      "| text:", chats[0][5:].decode(errors="replace") if chats else None)
ok &= bool(mismatch) and bool(chats)

# 3. other protocol is refused too
s.sendto(packet(bytes([REQUESTENTER, 3]) + b"New\x00" + trailer(layout, proto=3, ver=b"0.3.0")), SERVER)
got = recv_all(s)
refused = any(len(g) >= 2 and g[0] == SIMPLE and g[1] == VERSIONMISMATCH for g in got)
print("protocol 3 REQUESTENTER ->", "VERSIONMISMATCH" if refused else "accepted?!")
ok &= refused

# 4. same version is accepted (ENTER)
s.sendto(packet(bytes([REQUESTENTER, 4]) + b"Same\x00" + trailer(layout)), SERVER)
got = recv_all(s, 2.5)
entered = any(len(g) >= 2 and g[0] == SIMPLE and g[1] == ENTER for g in got)
print("same-version REQUESTENTER ->", "ENTER" if entered else "no ENTER (types: %s)" % [g[:2] for g in got])
ok &= entered

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
