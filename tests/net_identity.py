"""Join bookkeeping checks against a running local server (roadmap M3):
a repeated join request from the same address gets the same slot and port
(no ghost slots), and another client gets its own slot and port.

    HK_AUTOSTART=1 <Edifice binary> &   then   python3 tests/net_identity.py
"""
import socket, struct, sys, time

SERVER = ("127.0.0.1", 27020)
SIMPLE, INFO, REQUESTENTER = 5, 2, 1
REQUESTINFO, ENTER = 1, 2
MAGIC = 0x31564445


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
    sock.settimeout(wait)
    out, end = [], time.time() + wait
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            break
        out += messages(data)
    return out


def new_socket():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    return s


probe = new_socket()
layout = None
for _ in range(40):
    probe.sendto(packet(simple(REQUESTINFO)), SERVER)
    infos = [m for m in recv(probe, 1.0) if m and m[0] == INFO]
    if infos:
        t = infos[0][2 + infos[0][1] + 1:]
        layout = struct.unpack_from("<I", t, 6)[0]
        break
if layout is None:
    print("FAIL: server not answering"); sys.exit(1)
trailer = struct.pack("<IHIB", MAGIC, 2, layout, 5) + b"0.2.0"


def join(sock, name):
    sock.sendto(packet(bytes([REQUESTENTER, len(name)]) + name + b"\x00" + trailer), SERVER)
    for m in recv(sock, 2.0):
        if len(m) >= 12 and m[0] == SIMPLE and m[1] == ENTER:
            port, slot = struct.unpack_from("<II", m, 4)
            return socket.ntohs(port & 0xFFFF), slot
    return None


a, b = new_socket(), new_socket()
first = join(a, b"Alice")
again = join(a, b"Alice")
other = join(b, b"Bob")
print("Alice:", first, " Alice again:", again, " Bob:", other)
ok = first is not None and again == first and other is not None and other[0] != first[0] and other[1] != first[1]
print("ALL OK" if ok else "SOME CHECKS FAILED")
sys.exit(0 if ok else 1)
