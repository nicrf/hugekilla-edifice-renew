"""Sends malformed packets to a running local server and checks it survives
(roadmap M2). Best run against an AddressSanitizer build:

    EDIFICE_CMAKE_ARGS="-DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_C_FLAGS=-fsanitize=address \
      -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address" python3 build.py --config Debug
    HK_AUTOSTART=1 build/macos-debug/Edifice.app/Contents/MacOS/Edifice &
    python3 tests/net_fuzz.py
"""
import os, socket, struct, sys, time

# HK_TEST_PORT: a server on another info port than the default 27020
SERVER = ("127.0.0.1", int(os.environ.get("HK_TEST_PORT", "27020")))
SIMPLE, INFO, REQUESTENTER, CHATMESSAGE, BLOCKS = 5, 2, 1, 8, 7
KILLEDPOSITION, UPDATESERVEROBJECT, VOICE, FIREGUN, THROWSMOKE = 14, 12, 16, 10, 20
INPUTCMD = 21
REQUESTINFO, ENTER, CREDITS, RECEIVEDLANDINFO = 1, 2, 24, 5
RELIABLE, LASTRELIABLE, ACK = 0xFF, 0xFE, 0xFD
MAGIC = 0x31564445


def packet(*msgs):
    out = b"\x00"
    for m in msgs:
        out += struct.pack("<H", len(m)) + m
    return out


def simple(type2, msg=0, msg2=0):
    return struct.pack("<bbxxII", SIMPLE, type2, msg, msg2)


def messages(data):
    if not data or data[0] != 0:
        return []
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


def reliable(kind, rid, num, payload):
    body = struct.pack("<HH", rid, num) + payload
    return bytes([kind]) + checksum(body) + body


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))


def drain(wait):
    s.settimeout(wait)
    got = []
    end = time.time() + wait
    while time.time() < end:
        try:
            data, addr = s.recvfrom(65536)
        except socket.timeout:
            break
        got.append((addr, data))
    return got


def server_alive(tries=10):
    for _ in range(tries):
        s.sendto(packet(simple(REQUESTINFO)), SERVER)
        for addr, data in drain(1.0):
            if any(m and m[0] == INFO for m in messages(data)):
                return True
    return False


if not server_alive(40):
    print("FAIL: server not answering"); sys.exit(1)

# layout hash from INFO, to join with a valid version
s.sendto(packet(simple(REQUESTINFO)), SERVER)
info = [m for a, d in drain(1.0) for m in messages(d) if m and m[0] == INFO][0]
t = info[2 + info[1] + 1:]
proto, layout = struct.unpack_from("<HI", t, 4)
trailer = struct.pack("<IHIB", MAGIC, proto, layout, 5) + b"0.2.0"

# join to get a per-client port (clientsock), where game messages go
s.sendto(packet(bytes([REQUESTENTER, 4]) + b"Fuzz\x00" + trailer), SERVER)
client_port = None
for addr, data in drain(2.5):
    for m in messages(data):
        if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
            # ENTER.msg = the server's per-client port, in network byte order
            (port_field,) = struct.unpack_from("<I", m, 4)
            client_port = (SERVER[0], socket.ntohs(port_field & 0xFFFF))
print("joined, client socket at", client_port)
if not client_port:
    print("FAIL: no ENTER"); sys.exit(1)

attacks = [
    ("grouped size larger than the datagram", b"\x00" + struct.pack("<H", 60000) + b"A" * 20),
    ("reliable part number 65535 (write 29 MB away)", reliable(RELIABLE, 1, 0xFFFF, b"B" * 400)),
    ("last reliable with a huge inner size", reliable(LASTRELIABLE, 1, 0, struct.pack("<I", 0x7FFFFFFF) + b"C" * 20)),
    ("reliable shorter than its header", bytes([RELIABLE]) + b"\x00\x00\x00"),
    ("truncated ACK", bytes([ACK]) + b"\x01"),
    ("chat with length 60000", packet(bytes([CHATMESSAGE, 0, 0]) + struct.pack("<H", 60000) + b"D" * 50)),
    ("BLOCKS claiming 4 billion blocks", packet(bytes([BLOCKS]) + struct.pack("<I", 0xFFFFFFFF) + b"\x01" * 18)),
    ("killed position with killer 200", packet(bytes([KILLEDPOSITION, 1, 200]) + b"\x00" * 13)),
    ("a protocol 5 position (type 3, no longer one)", packet(bytes([3, 120, 50, 0, 10]) + b"\x00" * 13)),
    ("input commands, n 200", packet(bytes([INPUTCMD, 200]) + b"\x01" * 40)),
    ("input commands cut short", packet(bytes([INPUTCMD, 4]) + struct.pack("<II", 7, 0) + b"\x02" * 15)),
    ("input commands with NaN-free garbage", packet(bytes([INPUTCMD, 4]) + struct.pack("<II", 0xFFFFFFF0, 0xFFFFFFFF) + b"\xff" * 40)),
    ("a shot without its tick", packet(bytes([FIREGUN, 0, 0]) + b"\x00" * 24)),
    ("a shot at a tick 4 billion ago", packet(bytes([FIREGUN, 0, 0]) + struct.pack("<6f", 0, 0, 1, 1e30, 1e30, 1e30) +
                                              struct.pack("<IBIH", 1, 255, 0xFFFFFFFF, 0xFFFF))),
    ("a smoke grenade cut in its tick", packet(bytes([THROWSMOKE, 0, 0]) + b"\x00" * 24 + b"\x01\x02")),
    ("fake RECEIVEDLANDINFO", packet(simple(RECEIVEDLANDINFO))),
    ("kill credited to player 4000", packet(simple(19, 0, 4000))),
    ("team 77", packet(simple(20, 0, 77))),
    ("voice spoofing player 200", packet(struct.pack("<BBHH", VOICE, 200, 1, 10) + b"E" * 10)),
    ("voice frame of 60000 bytes", packet(struct.pack("<BBHH", VOICE, 0, 2, 60000) + b"F" * 900)),
    ("voice frame longer than its message", packet(struct.pack("<BBHH", VOICE, 0, 3, 400) + b"G" * 50)),
    ("voice with no frame", packet(struct.pack("<BBHH", VOICE, 0, 4, 0))),
    ("truncated voice header", packet(bytes([VOICE, 1, 2]))),
    ("voice frame of 401 bytes", packet(struct.pack("<BBHH", VOICE, 0, 5, 401) + b"H" * 401)),
]
for name, data in attacks:
    s.sendto(data, client_port)
    s.sendto(data, SERVER)   # and on the info port
    time.sleep(0.15)
    print("sent:", name)

time.sleep(1.0)
ok = server_alive()
print("server still answering:", ok)
print("ALL OK" if ok else "SERVER DOWN")
sys.exit(0 if ok else 1)
