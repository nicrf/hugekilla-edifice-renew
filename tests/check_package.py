#!/usr/bin/env python3
"""Checks a release archive of build.py --package, as a player would get it:
    python3 tests/check_package.py dist/Edifice-<version>-<os>.(zip|tar.gz) [--gns]

Unpacks it into a temporary folder, then
  - contents: the game, data/, license texts (with --gns: licenses/ of
    GameNetworkingSockets, protobuf, abseil, OpenSSL);
  - dependencies: only the OS's own libraries or what the archive ships
    (macOS: otool -L, both architectures, signature; Linux: readelf, nothing of
    GameNetworkingSockets/protobuf/OpenSSL linked dynamically or exported;
    Windows: dumpbin /dependents of every exe and DLL, C++ runtime app-local);
  - a short run: the unpacked game started as a headless server (HK_AUTOSTART=1,
    with --gns HK_TRANSPORT=gns, which needs GameNetworkingSockets and OpenSSL to
    start: "gns: listening on port"), then tests/net_handshake.py against it
    (only a warning on Windows, where the runners have no OpenGL 2).
Linux needs a display (xvfb-run). Used by the CI on the three OS. --no-run skips
the run: the server it starts uses the per-user net.ini where HOME/APPDATA don't
move it (macOS), and would announce itself if servervisibility isn't lan there."""
import glob
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SYSTEM = platform.system()
errors = []


def fail(msg):
    print("FAIL: " + msg, flush=True)
    errors.append(msg)


def warn(msg):
    print("WARNING: " + msg, flush=True)


def out(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True).stdout


def unpack(archive, dest):
    if archive.endswith(".tar.gz"):
        with tarfile.open(archive) as t:
            t.extractall(dest)
    elif SYSTEM == "Darwin":
        subprocess.check_call(["ditto", "-x", "-k", archive, dest])   # keeps modes and symlinks
    else:
        with zipfile.ZipFile(archive) as z:
            z.extractall(dest)
    tops = os.listdir(dest)
    if len(tops) != 1:
        fail("one top folder expected in the archive, got %s" % tops)
    return os.path.join(dest, tops[0])


def check_contents(top, gns):
    for f in ("license.txt", "lgpl-3.0.txt"):
        if not os.path.isfile(os.path.join(top, f)):
            fail("%s missing" % f)
    lic = os.path.join(top, "licenses")
    have = sorted(os.listdir(lic)) if os.path.isdir(lic) else []
    print("licenses/: " + ", ".join(have))
    need = ["SDL2.txt", "opus.txt"]
    if gns:
        need += ["gamenetworkingsockets.txt", "protobuf.txt", "abseil.txt", "openssl.txt"]
    for n in need:
        if n not in have:
            fail("licenses/%s missing" % n)


def check_macos(top, gns):
    app = os.path.join(top, "Edifice.app")
    exe = os.path.join(app, "Contents", "MacOS", "Edifice")
    if not os.path.isdir(os.path.join(app, "Contents", "Resources", "data")):
        fail("Edifice.app/Contents/Resources/data missing")
    archs = out(["lipo", "-archs", exe]).split()
    print("architectures: " + " ".join(archs))
    for a in ("arm64", "x86_64"):
        if a not in archs:
            fail("no %s in the executable" % a)
    for a in archs:
        txt = out(["otool", "-arch", a, "-l", exe])
        m = re.search(r"LC_BUILD_VERSION.*?minos ([0-9.]+)", txt, re.S) or \
            re.search(r"LC_VERSION_MIN_MACOSX.*?version ([0-9.]+)", txt, re.S)
        print("%s: minimum macOS %s" % (a, m.group(1) if m else "?"))
    fw = os.path.join(app, "Contents", "Frameworks")
    for b in [exe] + glob.glob(os.path.join(fw, "*.dylib")):
        # a fat binary lists its dependencies once per architecture ("<file> (architecture x):")
        lines = sorted({l for l in out(["otool", "-L", b]).splitlines() if not l.rstrip().endswith(":")})
        for l in lines:
            dep = l.strip().split(" (")[0]
            if dep.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dep.startswith("@executable_path/../Frameworks/") or dep.startswith("@rpath/"):
                if os.path.isfile(os.path.join(fw, os.path.basename(dep))):
                    continue
                if os.path.basename(dep) == os.path.basename(b):
                    continue    # a bundled dylib's own id
            fail("%s links %s, not in the bundle nor the system" % (os.path.basename(b), dep))
        print("%s: %s" % (os.path.basename(b), ", ".join(os.path.basename(l.strip().split(" (")[0]) for l in lines)))
    r = subprocess.run(["codesign", "--verify", "--deep", "--strict", app])
    if r.returncode:
        fail("codesign --verify failed")
    return exe


def check_linux(top, gns):
    exe = os.path.join(top, "Edifice")
    if not os.path.isdir(os.path.join(top, "data")):
        fail("data/ missing")
    needed = re.findall(r"\(NEEDED\).*\[(.*)\]", out(["readelf", "-d", exe]))
    print("NEEDED: " + ", ".join(needed))
    for n in needed:
        if re.search(r"GameNetworkingSockets|protobuf|absl|libssl|libcrypto", n):
            fail("%s linked dynamically" % n)
    ldd = out(["ldd", exe])
    if "not found" in ldd:
        fail("missing libraries:\n" + ldd)
    exported = out(["nm", "-D", "--defined-only", exe])
    leaks = [l.split()[-1] for l in exported.splitlines() if re.search(r" (EVP_|SSL_|OPENSSL_|CRYPTO_)", l)]
    if leaks:
        fail("OpenSSL symbols exported (libcurl's libssl would use them): " + ", ".join(leaks[:5]))
    glibc = sorted(set(re.findall(r"GLIBC_([0-9.]+)", out(["objdump", "-T", exe]))),
                   key=lambda v: [int(x) for x in v.split(".")])
    print("newest glibc symbol: GLIBC_%s" % (glibc[-1] if glibc else "?"))
    return exe


def find_dumpbin():
    d = shutil.which("dumpbin")
    if d:
        return d
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    inst = out([vswhere, "-latest", "-products", "*", "-property", "installationPath"]).strip()
    cands = sorted(glob.glob(os.path.join(inst, "VC", "Tools", "MSVC", "*", "bin", "Hostx64", "x64", "dumpbin.exe")))
    return cands[-1] if cands else None


# the Visual C++ redistributable: must be in the archive, never taken from System32
REDIST = re.compile(r"^(vcruntime|msvcp|concrt|vccorlib|vcomp)\d+.*\.dll$|^ucrtbased\.dll$", re.I)


def check_windows(top, gns):
    exe = os.path.join(top, "Edifice.exe")
    if not os.path.isdir(os.path.join(top, "data")):
        fail("data/ missing")
    dumpbin = find_dumpbin()
    if not dumpbin:
        fail("dumpbin not found (Visual Studio)")
        return exe
    shipped = {f.lower() for f in os.listdir(top)}
    print("DLLs: " + ", ".join(sorted(f for f in os.listdir(top) if f.lower().endswith(".dll"))))
    system32 = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")
    for b in [exe] + glob.glob(os.path.join(top, "*.dll")):
        txt = out([dumpbin, "/nologo", "/dependents", b])
        deps = re.findall(r"^\s+(\S+\.dll)\s*$", txt, re.M | re.I)
        print("%s: %s" % (os.path.basename(b), ", ".join(deps)))
        for d in deps:
            if d.lower() in shipped:
                continue
            if REDIST.match(d):
                fail("%s needs %s (Visual C++ runtime), not in the archive" % (os.path.basename(b), d))
            elif not (d.lower().startswith(("api-ms-win-", "ext-ms-")) or os.path.isfile(os.path.join(system32, d))):
                fail("%s needs %s, neither in the archive nor in System32" % (os.path.basename(b), d))
    if gns and "gamenetworkingsockets.dll" not in shipped:
        fail("GameNetworkingSockets.dll missing")
    return exe


def run_game(exe, gns, tmp):
    env = dict(os.environ)
    home = os.path.join(tmp, "home")       # fresh settings folder (copied from data/)
    os.makedirs(home)
    env.update(HOME=home, XDG_DATA_HOME=home, APPDATA=home, SDL_AUDIODRIVER="dummy",
               HK_AUTOSTART="1", HK_CONSOLE_LOG="1", HK_SHOT=os.devnull, HK_SHOT_FRAME="999999")
    env.pop("HK_P2P", None)
    if gns:
        env["HK_TRANSPORT"] = "gns"
    log = os.path.join(tmp, "game.log")
    with open(log, "w") as f:
        # from another folder: the game must find its data next to itself
        p = subprocess.Popen([exe], stdout=f, stderr=subprocess.STDOUT, env=env, cwd=tmp)
    ok = True
    try:
        end = time.time() + 90
        want = "gns: listening on port" if gns else None
        while want and time.time() < end:
            if p.poll() is not None:
                break
            with open(log, errors="replace") as f:
                if want in f.read():
                    break
            time.sleep(0.5)
        if p.poll() is not None:
            code = p.returncode & 0xffffffff
            fail("the game exited with code %d (0x%08x)%s" % (p.returncode, code,
                 ": a DLL is missing" if code == 0xC0000135 else ""))
            ok = False
        elif want:
            with open(log, errors="replace") as f:
                if want in f.read():
                    print("game: " + want)
                else:
                    fail("no '%s' within 90 s" % want)
                    ok = False
        if ok:
            # The Windows runners have no GPU, only Windows' own OpenGL 1.1 (GDI),
            # and the game exits soon after starting there. The run then proves that
            # every DLL loads and GameNetworkingSockets starts; the handshake is
            # informative only.
            report = warn if SYSTEM == "Windows" else fail
            r = subprocess.run([sys.executable, os.path.join(HERE, "net_handshake.py")], timeout=120)
            if r.returncode:
                report("tests/net_handshake.py against the unpacked game")
            if p.poll() is not None:
                report("the game exited during the handshake test, code %d (0x%08x)" %
                       (p.returncode, p.returncode & 0xffffffff))
    finally:
        if p.poll() is None:
            p.kill()
        p.wait()
        with open(log, errors="replace") as f:
            txt = f.read()
        print("-- game log (tail):\n" + "\n".join(txt.splitlines()[-25:]))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    gns = "--gns" in sys.argv
    if len(args) != 1:
        sys.exit(__doc__)
    archive = os.path.abspath(args[0])
    print("%s: %.1f MB" % (os.path.basename(archive), os.path.getsize(archive) / 1e6))
    tmp = tempfile.mkdtemp(prefix="edifice-pkg-")
    try:
        top = unpack(archive, os.path.join(tmp, "unpacked"))
        check_contents(top, gns)
        exe = {"Darwin": check_macos, "Linux": check_linux, "Windows": check_windows}[SYSTEM](top, gns)
        if "--no-run" not in sys.argv:
            run_game(exe, gns, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if errors:
        print("%d problem(s) with %s" % (len(errors), os.path.basename(archive)))
        sys.exit(1)
    print("OK: " + os.path.basename(archive))


if __name__ == "__main__":
    main()
