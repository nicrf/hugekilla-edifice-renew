"""A server started with a saved map (format 2, see src/game/mapfile.h) sends
that map to the clients that join: its land and sky in LANDINFO, and every
one of its blocks in BLOCKS.
    python3 tests/net_map.py --write FILE      writes the test map (this script's
                                               own writer of the format)
    HK_AUTOSTART=1 HK_MAPFILE=FILE <Edifice binary> &    (FILE: absolute path)
    python3 tests/net_map.py FILE              joins (tests/net_join.py) and checks
tests/run_netplay_tests.sh does both."""
import os, struct, subprocess, sys, tempfile, zlib

GRID, HEIGHT, NUMDEF = 256, 120, 160
LANDINFO, BLOCKS = 4, 7


def sky():
    # SkySettings: 11 f32, 4 int32, 2 f32, cloudtex/misttex/sound[60], int32 numlillypad
    names = b"".join(n.ljust(60, b"\0") for n in (b"nelso.bmp", b"mist.pxr", b"peacefulnoise.wav"))
    return struct.pack("<11f4i2f", .8, .57, .66, .96, .001, .002, .009, .007, .5, .2, .2,
                       1, 0, 1, 0, .9, 140) + names + struct.pack("<i", 30)


def landdefs():
    # LandDefs: int32 numdefs, NUMDEF x (int8 type, int32 sx sy sh sw, f32 sc sd sf),
    # groundtex[9][60], int32 numgroundtex, uint32 randomseed numscenery numblotch
    out = struct.pack("<i", 40)
    for i in range(NUMDEF):
        # type 0 ridges get sw=70 from AddRidge (land.cpp), which the server then sends
        sw = 70 if i % 3 == 0 else (3 + i % 10) ** 2
        out += struct.pack("<b4i3f", i % 3, (i * 37) % 512, (i * 91) % 512, 10 + i % 20, sw,
                           .1 + i * .003, 3.7 + i * .01, .3 + .1 * (i % 5))
    tex = [b"grassdirt.bmp", b"grassdirtblotch1.bmp", b"grassdirtblotch2.bmp", b"snowgrassblotch1.bmp"]
    out += b"".join(t.ljust(60, b"\0") for t in tex + [b""] * 5)
    return out + struct.pack("<iIII", 4, 4242, 10, 150)


def blocks():
    """{(x, y index, z): (type, top, side, bottom)}: a two-layer wall and a few ramps"""
    b = {}
    for x in range(40, 50):
        for z in range(60, 70):
            for y in (5, 6):
                b[(x, y, z)] = (1, x % 10, z % 10, (x + z) % 10)
    for i in range(4):
        b[(100 + i, 3, 30)] = (2 + i, 1, 2, 3)
    b[(200, 50, 250)] = (1, 9, 9, 9)
    return b


def write_map(path):
    built = blocks()
    runs = []

    def add(n, cell):
        if n <= 0:
            return
        if runs and runs[-1][1] == cell:
            runs[-1][0] += n
        else:
            runs.append([n, cell])

    for y in range(HEIGHT):   # file order: y, then x, then z (fastest)
        empty = bytes([0, 0, 0, 0, 0, HEIGHT - y])   # as CreateLand leaves it
        cells = sorted((x * GRID + z, v) for (x, yy, z), v in built.items() if yy == y)
        pos = 0
        for p, (t, top, side, bottom) in cells:
            add(p - pos, empty)
            add(1, bytes([63, t, top, side, bottom, 1]))
            pos = p + 1
        add(GRID * GRID - pos, empty)
    data = b"EDIFICEMAP" + struct.pack("<5H", 2, 5, GRID, HEIGHT, NUMDEF) + sky() + landdefs()
    data += b"".join(struct.pack("<I", n) + c for n, c in runs)
    data += struct.pack("<IB", GRID * HEIGHT * GRID, 0)
    data += struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(data)
    print("wrote %s: %d bytes, %d blocks" % (path, len(data), len(built)))


def check(path):
    with open(path, "rb") as f:
        data = f.read()
    filesky, filedefs = data[20:20 + 252], data[20 + 252:20 + 252 + 5200]
    dump = tempfile.mkdtemp(prefix="net_map")
    env = dict(os.environ, NET_JOIN_DUMP=dump)
    joined = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "net_join.py")], env=env)
    ok = joined.returncode == 0
    print(("ok   " if ok else "FAIL ") + "joined the server (tests/net_join.py)")
    files = os.listdir(dump)
    land = [open(os.path.join(dump, n), "rb").read() for n in files if n.startswith("msg-%d-" % LANDINFO)]
    gotdefs = land[0][1:1 + 5200] if land else b""
    gotsky = land[0][1 + 5200:1 + 5200 + 252] if land else b""
    good = gotdefs == filedefs and gotsky == filesky
    print(("ok   " if good else "FAIL ") + "LANDINFO carries the map's land and sky")
    if land and not good:
        diff = [i for i in range(min(len(gotdefs), len(filedefs))) if gotdefs[i] != filedefs[i]]
        print("     defs differ at %s, sky %s" % (diff[:10], gotsky == filesky))
    ok &= good
    received = {}
    for n in files:
        if n.startswith("msg-%d-" % BLOCKS):
            m = open(os.path.join(dump, n), "rb").read()
            (count,) = struct.unpack_from("<I", m, 1)
            for i in range(min(count, (len(m) - 5) // 9)):
                x, y, z, t, team, top, side, bottom, delete = m[5 + 9 * i:14 + 9 * i]
                if not delete:
                    received[(x, y, z)] = (t, top, side, bottom)
    # the server sends block index y as coordinate y-1 (GetBlock adds 1, land.cpp)
    expected = {(x, y - 1, z): v for (x, y, z), v in blocks().items()}
    good = received == expected
    print(("ok   " if good else "FAIL ") + "BLOCKS: %d block(s) received, %d in the map" % (len(received), len(expected)))
    ok &= good
    print("ALL OK" if ok else "SOME CHECKS FAILED")
    return ok


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--write":
        write_map(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(0 if check(sys.argv[1]) else 1)
