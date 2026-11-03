"""Proximity voice chat relay (VOICE message, src/net/voicemsg.h):
    HK_TESTPLACE=1 HK_AUTOSTART=1 <Edifice binary> &  then  python3 tests/net_voice.py
Three clients of protocol 6 join a local server (tests/netclient.py: input commands,
the server moves them; HK_TESTPLACE puts them where the test wants): A and B on the
very same spot, C far away; the host sets every player on the ground or the blocks
under it, and the world is random: they are put there at y=120, the voice frames
come once they stand. The world-edge wrap is covered by voice_test.
A sends VOICE frames with a spoofed player field: B must get them as coming from
A's slot, C must get nothing. Then B moves away and gets nothing either; malformed
VOICE messages are never relayed. Last, A stands next to the host and sends garbage
frames, which the host decodes (opus must reject them)."""
import os, struct, sys, time
from netclient import *


def voice(player, seq, frame):
    return struct.pack("<BBHH", VOICE, player, seq, len(frame)) + frame


def voices(cl, mark):
    """(player, seq, frame) of the VOICE messages since mark"""
    out = []
    for m in cl.since(mark):
        if m[0] == VOICE and len(m) >= 6:
            p, seq, n = struct.unpack_from("<BHH", m, 1)
            out.append((p, seq, m[6:6 + n]))
    return out


trailer, caps, info = server_info()
game = Game()
check = game.check
clients = [Client(n) for n in ("Alice", "Bob", "Carol")]
for n, c in enumerate(clients):
    if not c.join(trailer, clients[:n]):
        print("FAIL: %s could not join" % c.name.decode()); sys.exit(1)
    game.add(c)
    print("%s joined as slot %d" % (c.name.decode(), c.slot))
a, b, c = clients


def place(bx):
    """puts them there, then lets the host set everyone on the ground"""
    b.place(bx, 120, 100)
    c.place(5, 120, 350)
    a.place(1, 120, 100)
    game.settle(clients, 6)


def talk(first, count, frames=None):
    """A's VOICE frames, one a tick, while everyone plays"""
    mark = [cl.mark() for cl in clients]
    frames = frames or (lambda k: voice(c.slot, first + k, bytes([0x40 + k]) * 40))
    game.pump(count / 60 + 0.5, each=lambda k: a.send(frames(k)) if k < count else None)
    return [voices(cl, m) for cl, m in zip(clients, mark)]


game.pump(0.5)
# near (A and B at the same spot) and far
place(1)
va, vb, vc = talk(100, 10)
check(len(vb) >= 8, "near player got %d/10 frames" % len(vb))
check(all(p == a.slot for p, s, f in vb), "relayed as the sender's slot %d (not the spoofed %d)" % (a.slot, c.slot))
check(all(f == bytes([0x40 + s - 100]) * 40 for p, s, f in vb), "frames intact, sequence kept")
check(not vc, "far player got nothing (%d)" % len(vc))
check(not va, "sender doesn't get its own voice back (%d)" % len(va))

# malformed VOICE: truncated, oversized, empty, length past the end
bad = [
    bytes([VOICE, 0, 1]),
    struct.pack("<BBHH", VOICE, 0, 2, 401) + b"X" * 401,
    struct.pack("<BBHH", VOICE, 0, 3, 0),
    struct.pack("<BBHH", VOICE, 0, 4, 300) + b"Y" * 20,
    struct.pack("<BBHH", VOICE, 0, 5, 20) + b"Z" * 30,
]
va, vb, vc = talk(0, len(bad), lambda k: bad[k])
check(not vb and not vc, "malformed VOICE never relayed (%d, %d)" % (len(vb), len(vc)))

# B walks away: out of range
place(200)
va, vb, vc = talk(200, 10)
check(not vb, "player out of range got nothing (%d)" % len(vb))

# back in range
place(1)
va, vb, vc = talk(300, 5)
check(len(vb) >= 4, "back in range: %d/5 frames" % len(vb))

# garbage opus frames next to the host, who plays them
host = a.others().get(0)
if host:
    a.place(host[0] + 2, host[1], host[2])
    game.pump(0.5)
    talk(400, 30, lambda k: voice(0, 400 + k, os.urandom(1 + k * 13)))
    print("sent garbage frames next to the host at (%.0f, %.0f, %.0f)" % host[0:3])
else:
    print("host position unknown, garbage frames skipped")
game.pump(0.5)
_, _, info = server_info(3)
check(info, "server still answering")
game.finish()
