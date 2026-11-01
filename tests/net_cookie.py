"""Join cookie checks against a running local server (roadmap M11 phase 2,
docs/M11.md): a client that announces HELLOCAP_COOKIE gets a slot only after
sending back the cookie of its address (ENTERCOOKIE), a wrong, foreign or
expired cookie takes no slot, the answer is never bigger than the request, and
an old-style REQUESTENTER (no capabilities) still joins, unless the server runs
with ac_requirecookie 1 (then: VERSIONMISMATCH only, no slot).

    HK_AC_VARS="ac_cookietime=1000 ac_ipenter=600" HK_AUTOSTART=1 <binary> &
    HK_AC_VARS=... python3 tests/net_cookie.py            (ac_requirecookie 0)
    HK_AC_VARS="... ac_requirecookie=1" ... python3 tests/net_cookie.py require

The test reads HK_AC_VARS for ac_cookietime (the expiry check needs it short)
and the mode. tests/run_netplay_tests.sh runs both."""
import os, socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
SIMPLE, INFO, REQUESTENTER, CHATMESSAGE = 5, 2, 1, 8
REQUESTINFO, ENTER, VERSIONMISMATCH, ENTERCOOKIE = 1, 2, 26, 27
CAP_COOKIE, CAP_HASCOOKIE, CAP_REQUIRECOOKIE = 1, 2, 4
MAGIC = 0x31564445

acvars = dict(kv.split("=", 1) for kv in os.environ.get("HK_AC_VARS", "").replace(",", " ").split() if "=" in kv)
require = "require" in sys.argv[1:]
cookietime = int(acvars.get("ac_cookietime", "30000"))
if require != (acvars.get("ac_requirecookie", "0") == "1"):
    print("FAIL: HK_AC_VARS ac_requirecookie and the 'require' argument disagree"); sys.exit(1)


def packet(m):
    return b"\x00" + struct.pack("<H", len(m)) + m


def simple(type2):
    return struct.pack("<bbxxII", SIMPLE, type2, 0, 0)


def messages(data):
    if not data or data[0] != 0:
        return []
    pos, out = 1, []
    while pos + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, pos)
        out.append(data[pos + 2:pos + 2 + n])
        pos += 2 + n
    return out


def recv(sock, wait):
    """[(datagram size, [messages])]"""
    sock.settimeout(wait)
    out, end = [], time.time() + wait
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            break
        out.append((len(data), messages(data)))
    return out


def newsock():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    return s


ok = True


def check(cond, what):
    global ok
    print(("ok   " if cond else "FAIL ") + what)
    ok &= bool(cond)


# server up, and its INFO: protocol, version, details, caps
info = None
q = newsock()
for _ in range(40):
    q.sendto(packet(simple(REQUESTINFO)), SERVER)
    got = [m for _, ms in recv(q, 1.0) for m in ms if m and m[0] == INFO]
    if got:
        info = got[0]
        break
if not info:
    print("FAIL: no INFO reply"); sys.exit(1)
t = info[2 + info[1] + 1:]
magic, proto, layout, vlen = struct.unpack_from("<IHIB", t, 0)
ver = t[11:11 + vlen]
rest = t[11 + vlen:]
caps = rest[4] if len(rest) >= 5 else 0
print("server protocol %d version %s, INFO caps %d" % (proto, ver.decode(), caps))
check(caps & CAP_COOKIE, "INFO announces join cookies")
check(bool(caps & CAP_REQUIRECOOKIE) == require, "INFO says ac_requirecookie %d" % require)


def players():
    q.sendto(packet(simple(REQUESTINFO)), SERVER)
    for _, ms in recv(q, 0.6):
        for m in ms:
            if m and m[0] == INFO:
                r = m[2 + m[1] + 1:][11 + vlen:]
                return r[0]
    return None


def request(sock, name, capsbyte=None, cookie=b""):
    m = bytes([REQUESTENTER, len(name)]) + name + b"\x00" + struct.pack("<IHIB", MAGIC, proto, layout, len(ver)) + ver
    if capsbyte is not None:
        m += bytes([capsbyte | (CAP_HASCOOKIE if cookie else 0)]) + cookie
    p = packet(m)
    sock.sendto(p, SERVER)
    return len(p)


def answer(sock, wait=1.0):
    """(cookie or None, ENTER slot or None, VERSIONMISMATCH, chat seen, biggest datagram)"""
    cookie = slot = None
    mismatch = chat = False
    biggest = 0
    for size, ms in recv(sock, wait):
        biggest = max(biggest, size)
        for m in ms:
            if len(m) >= 12 and m[0] == SIMPLE:
                _, type2, a, b = struct.unpack_from("<bbxxII", m, 0)
                if type2 == ENTERCOOKIE:
                    cookie = struct.pack("<II", a, b)
                elif type2 == ENTER:
                    slot = b
                elif type2 == VERSIONMISMATCH:
                    mismatch = True
            elif m and m[0] == CHATMESSAGE:
                chat = True
    return cookie, slot, mismatch, chat, biggest


start = players()
print("players at start:", start)

# 1. a new client: a cookie, no slot, an answer no bigger than the request
a = newsock()
sent = request(a, b"Cookie", CAP_COOKIE)
cookie, slot, _, _, biggest = answer(a)
check(cookie is not None and slot is None, "first request -> ENTERCOOKIE only")
check(0 < biggest <= sent, "answer %d bytes <= request %d bytes" % (biggest, sent))
check(players() == start, "no slot before the cookie")

# 2. a wrong cookie: a new challenge, no slot
bad = bytes([cookie[0] ^ 0x55]) + cookie[1:] if cookie else b"\x00" * 8
sent = request(a, b"Cookie", CAP_COOKIE, bad)
c2, slot, _, _, biggest = answer(a)
check(c2 is not None and slot is None, "wrong cookie -> ENTERCOOKIE again, no ENTER")
check(biggest <= sent, "answer %d bytes <= request %d bytes" % (biggest, sent))
check(players() == start, "no slot for a wrong cookie")

# 3. the cookie of another address (port): no slot (what a spoofer can't get)
b = newsock()
request(b, b"Thief", CAP_COOKIE, cookie or b"\x00" * 8)
c3, slot, _, _, _ = answer(b)
check(slot is None and c3 is not None and c3 != cookie, "cookie of another port -> its own cookie, no ENTER")
check(players() == start, "no slot for a borrowed cookie")

# 4. the right cookie, sent back at once (as the game does): ENTER, one slot more
request(a, b"Cookie", CAP_COOKIE)
cookie, _, _, _, _ = answer(a, 0.3)
request(a, b"Cookie", CAP_COOKIE, cookie or b"\x00" * 8)
_, slot, _, _, _ = answer(a, 2.0)
check(slot is not None, "right cookie -> ENTER (slot %s)" % slot)
check(players() == start + 1, "one slot taken")
# a retry from the same address keeps its slot
request(a, b"Cookie", CAP_COOKIE, cookie or b"")
_, slot2, _, _, _ = answer(a, 1.5)
check(slot2 == slot and players() == start + 1, "retried request -> same slot")
joined = 1

# 5. an expired cookie (older than the previous time bucket): no slot
if cookietime <= 5000:
    c = newsock()
    request(c, b"Late", CAP_COOKIE)
    old, _, _, _, _ = answer(c, 0.5)
    time.sleep(2 * cookietime / 1000.0 + 0.3)
    request(c, b"Late", CAP_COOKIE, old or b"\x00" * 8)
    fresh, slot, _, _, _ = answer(c, 0.5)
    check(slot is None and fresh is not None and fresh != old, "expired cookie -> a new cookie, no ENTER")
    check(players() == start + joined, "no slot for an expired cookie")
    request(c, b"Late", CAP_COOKIE, fresh or b"")
    _, slot, _, _, _ = answer(c, 2.0)
    check(slot is not None, "then the new cookie -> ENTER")
    joined += 1
else:
    print("SKIP expired cookie (ac_cookietime %d ms: set HK_AC_VARS ac_cookietime=1000)" % cookietime)

# 6. a build before cookies (no caps byte after the trailer)
d = newsock()
sent = request(d, b"OldBuild")
cookie, slot, mismatch, chat, biggest = answer(d, 2.0)
if require:
    check(mismatch and slot is None and cookie is None, "old-style request refused (VERSIONMISMATCH)")
    check(not chat and biggest <= sent, "refusal %d bytes <= request %d bytes, no text" % (biggest, sent))
    check(players() == start + joined, "no slot for the old-style request")
else:
    check(slot is not None and cookie is None, "old-style request joins (ac_requirecookie 0)")
    joined += 1
    check(players() == start + joined, "and takes its slot")

print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
