#!/usr/bin/env python3
"""Build Edifice (HugeKilla) on macOS, Linux or Windows.

    python3 build.py [--config Debug|Release] [--clean] [--run] [--package] [--test]
                     [--no-gns] [--no-webrtc] [--universal] [--web]

Configures into build/<os>-<config>[-gns][-universal], builds, and optionally runs
the game or its unit tests, or packages it into dist/Edifice-<version>-<os>.(zip|tar.gz).
GameNetworkingSockets (P2P Internet games) is in by default, as in the releases,
through vcpkg (VCPKG_ROOT, else fetched into build/vcpkg); --no-gns leaves it out.
With it comes libdatachannel (EDIFICE_WEBRTC), so that browser players join the
games this build hosts; --no-webrtc leaves that out.
--web builds the web version instead (Emscripten: EMSDK, else build/emsdk at the
version of release.yml) into build/web[-debug]: --run serves it on localhost,
--package zips the site into dist/Edifice-<version>-web.zip.
Python 3 stdlib only.
"""
import argparse
import glob
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(ROOT, "data")
# --universal: oldest macOS the release runs on (same as cmake/triplets/universal-osx.cmake)
MACOS_MIN = "10.15"   # std::filesystem (src/platform/fileio.cpp)


def host_os():
    s = platform.system()
    if s == "Darwin":
        return "macos"
    if s == "Windows":
        return "windows"
    if s == "Linux":
        return "linux"
    return s.lower()


OS = host_os()

HINTS = {
    # SDL2 itself is downloaded and built by CMake; these are only the toolchain
    # and the system headers SDL needs (X11/Wayland, audio, GL).
    "macos": "  Install Xcode command line tools (xcode-select --install), then:\n"
             "    brew install cmake ninja",
    "linux": "  Debian/Ubuntu: sudo apt install build-essential cmake ninja-build libglu1-mesa-dev \\\n"
             "                 libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \\\n"
             "                 libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev \\\n"
             "                 libcurl4-openssl-dev\n"
             "  Fedora:        sudo dnf install gcc-c++ cmake ninja-build mesa-libGLU-devel libX11-devel \\\n"
             "                 libXext-devel libXrandr-devel libXcursor-devel libXi-devel alsa-lib-devel pulseaudio-libs-devel \\\n"
             "                 libcurl-devel",
    "windows": "  Install Visual Studio (Desktop development with C++ workload) and CMake\n"
               "  (CMake ships with Visual Studio; or: winget install Kitware.CMake).",
}


def die(msg, hint=True):
    print("error: " + msg, file=sys.stderr)
    if hint and OS in HINTS:
        print("Missing dependencies? \n" + HINTS[OS], file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    print("+ " + " ".join('"%s"' % c if " " in c else c for c in cmd), flush=True)
    r = subprocess.call(cmd, **kw)
    if r != 0:
        die("command failed with exit code %d" % r)


def project_version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r"project\s*\([^)]*VERSION\s+([0-9.]+)", f.read())
    return m.group(1) if m else "0.0.0"


def find_cmake():
    cmake = shutil.which("cmake")
    if not cmake and OS == "windows":
        # CMake bundled with Visual Studio
        vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                               "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.path.exists(vswhere):
            try:
                inst = subprocess.check_output([vswhere, "-latest", "-property", "installationPath"],
                                               universal_newlines=True).strip()
                cand = os.path.join(inst, "Common7", "IDE", "CommonExtensions", "Microsoft",
                                    "CMake", "CMake", "bin", "cmake.exe")
                if os.path.exists(cand):
                    cmake = cand
            except (OSError, subprocess.CalledProcessError):
                pass
    if not cmake:
        die("cmake not found in PATH")
    return cmake


def find_ctest(cmake):
    cand = os.path.join(os.path.dirname(cmake), "ctest.exe" if OS == "windows" else "ctest")
    if os.path.exists(cand):
        return cand
    return shutil.which("ctest") or die("ctest not found next to cmake")


def local_vcpkg():
    """vcpkg in build/vcpkg, at the commit the releases use (VCPKG_COMMIT of
    release.yml), cloned and bootstrapped the first time: no setup needed for the
    default GameNetworkingSockets build."""
    root = os.path.join(ROOT, "build", "vcpkg")
    exe = os.path.join(root, "vcpkg.exe" if OS == "windows" else "vcpkg")
    if os.path.isfile(exe):
        return root
    if not shutil.which("git"):
        die("GameNetworkingSockets needs vcpkg, and git to fetch it; or --no-gns", hint=False)
    wf = open(os.path.join(ROOT, ".github", "workflows", "release.yml")).read()
    m = re.search(r"VCPKG_COMMIT:\s*([0-9a-f]{40})", wf)
    if not m:
        die("VCPKG_COMMIT not found in release.yml", hint=False)
    print("Fetching vcpkg into %s (first GameNetworkingSockets build: its libraries "
          "take a while to compile, later builds reuse them)" % root, flush=True)
    os.makedirs(root, exist_ok=True)
    for cmd in (["git", "init", "-q"],
                ["git", "fetch", "-q", "--depth", "1", "https://github.com/microsoft/vcpkg", m.group(1)],
                ["git", "checkout", "-q", "FETCH_HEAD"],
                [os.path.join(root, "bootstrap-vcpkg.bat" if OS == "windows" else "bootstrap-vcpkg.sh"),
                 "-disableMetrics"]):
        if subprocess.call(cmd, cwd=root) != 0:
            shutil.rmtree(root, ignore_errors=True)
            die("could not set up vcpkg (%s); --no-gns builds without P2P" % " ".join(cmd), hint=False)
    return root


def local_emsdk():
    """emsdk: EMSDK (set by emsdk_env), else build/emsdk at the version the CI uses
    (EMSDK_VERSION of release.yml), fetched and installed the first time"""
    if os.environ.get("EMSDK"):
        return os.environ["EMSDK"]
    wf = open(os.path.join(ROOT, ".github", "workflows", "release.yml")).read()
    m = re.search(r"EMSDK_VERSION:\s*'?([0-9.]+)", wf)
    if not m:
        die("EMSDK_VERSION not found in release.yml", hint=False)
    version = m.group(1)
    root = os.path.join(ROOT, "build", "emsdk")
    tool = os.path.join(root, "emsdk.bat" if OS == "windows" else "emsdk")
    stamp = os.path.join(root, "upstream", "emscripten", "emscripten-version.txt")
    if os.path.isfile(stamp) and version in open(stamp).read():
        return root
    if not os.path.isfile(tool):
        if not shutil.which("git"):
            die("the web build needs emsdk (git to fetch it, or EMSDK set by emsdk_env)", hint=False)
        print("Fetching emsdk into %s" % root, flush=True)
        if subprocess.call(["git", "clone", "-q", "--depth", "1",
                            "https://github.com/emscripten-core/emsdk.git", root]) != 0:
            die("could not fetch emsdk", hint=False)
    for cmd in ([tool, "install", version], [tool, "activate", version]):
        if subprocess.call(cmd, cwd=root) != 0:
            die("emsdk %s failed (%s)" % (version, " ".join(cmd)), hint=False)
    return root


def web_env(emsdk):
    """the environment emsdk_env would give: emcc and Emscripten's Node.js (which
    runs the unit tests) first in PATH"""
    env = dict(os.environ, EMSDK=emsdk)
    paths = [os.path.join(emsdk, "upstream", "emscripten")]
    paths += sorted(glob.glob(os.path.join(emsdk, "node", "*", "bin")))[-1:]
    env["PATH"] = os.pathsep.join(paths + [env.get("PATH", "")])
    return env


def vcpkg_features(args):
    """the features of vcpkg.json a --gns build asks for"""
    return "gns;webrtc" if args.webrtc else "gns"


def gns_args(args, extra):
    """CMake arguments of a GameNetworkingSockets build: vcpkg toolchain, the
    "gns" feature of vcpkg.json (and "webrtc": libdatachannel), and the triplet
    of the releases."""
    root = os.environ.get("VCPKG_ROOT")
    if not root and shutil.which("vcpkg"):
        root = os.path.dirname(os.path.realpath(shutil.which("vcpkg")))
    if not root:
        root = local_vcpkg()
    toolchain = os.path.join(root, "scripts", "buildsystems", "vcpkg.cmake")
    if not os.path.isfile(toolchain):
        die("no vcpkg toolchain in %s (VCPKG_ROOT); --no-gns builds without P2P" % root, hint=False)
    cfg = ["-DEDIFICE_GNS=ON", "-DVCPKG_MANIFEST_FEATURES=" + vcpkg_features(args),
           "-DEDIFICE_WEBRTC=" + ("ON" if args.webrtc else "OFF"),
           "-DCMAKE_TOOLCHAIN_FILE=" + toolchain,
           "-DVCPKG_OVERLAY_TRIPLETS=" + os.path.join(ROOT, "cmake", "triplets")]
    if any(a.startswith("-DVCPKG_TARGET_TRIPLET") for a in extra):
        return cfg
    release = args.config == "Release"
    triplet = None                           # else vcpkg's default for the host
    if args.universal:
        triplet = "universal-osx"            # static, both architectures
    elif OS == "windows":
        # DLLs + shared C++ runtime (/MD): the static triplet doesn't link
        arch = "x86" if os.environ.get("EDIFICE_ARCH", "x64") == "Win32" else "x64"
        triplet = arch + "-windows" + ("-release" if release and arch == "x64" else "")
    elif OS == "linux" and release and platform.machine() in ("x86_64", "AMD64"):
        triplet = "x64-linux-release"        # static, like x64-linux
    if triplet:
        cfg.append("-DVCPKG_TARGET_TRIPLET=" + triplet)
    return cfg


def main():
    ap = argparse.ArgumentParser(description="Build Edifice (HugeKilla).")
    ap.add_argument("--config", choices=["Debug", "Release"], default="Release")
    ap.add_argument("--clean", action="store_true", help="delete the build directory first")
    ap.add_argument("--run", action="store_true", help="launch the game after building")
    ap.add_argument("--package", action="store_true", help="create an archive in dist/")
    ap.add_argument("--test", action="store_true", help="run the unit tests (ctest) after building")
    ap.add_argument("--generator", help="override the CMake generator (e.g. Ninja)")
    ap.add_argument("--gns", action="store_true", default=True,
                    help="with GameNetworkingSockets (P2P Internet games, the default), built by "
                         "vcpkg: VCPKG_ROOT, else one fetched into build/vcpkg")
    ap.add_argument("--no-gns", dest="gns", action="store_false",
                    help="without GameNetworkingSockets: no P2P, Internet games need open UDP ports")
    ap.add_argument("--no-webrtc", dest="webrtc", action="store_false", default=True,
                    help="with GameNetworkingSockets but without libdatachannel: browser players can't "
                         "join the games this build hosts")
    ap.add_argument("--sokol", action="store_true", default=True,
                    help="render with sokol_gfx: Metal on macOS, D3D11 on Windows, GL 4.1 core on Linux "
                         "(the default, as the releases)")
    ap.add_argument("--gl", dest="sokol", action="store_false",
                    help="render with the fixed-function OpenGL 2.1 backend instead (kept for one release, M13)")
    ap.add_argument("--universal", action="store_true",
                    help="macOS: one binary for Apple Silicon and Intel (macOS %s or later)" % MACOS_MIN)
    ap.add_argument("--web", action="store_true",
                    help="the web version (Emscripten, WebGL2) in build/web; --run serves it on localhost")
    args, game_args = ap.parse_known_args()
    if args.web:
        return main_web(args)
    if args.universal and OS != "macos":
        die("--universal is for macOS only", hint=False)

    cmake = find_cmake()
    name = "%s-%s" % (OS, args.config.lower())
    if args.gns:
        name += "-gns"
    if not args.sokol:
        name += "-gl"
    if args.universal:
        name += "-universal"
    bdir = os.path.join(ROOT, "build", name)
    if args.clean:
        if os.path.isdir(bdir):
            print("Removing " + bdir)
            shutil.rmtree(bdir)
        # Leftovers of the old single-tree build (cmake -B build), pre-restructure.
        top = os.path.join(ROOT, "build")
        if os.path.exists(os.path.join(top, "CMakeCache.txt")):
            for name in ("CMakeCache.txt", "CMakeFiles", "build.ninja", "cmake_install.cmake",
                         "Edifice.app", ".ninja_deps", ".ninja_log"):
                p = os.path.join(top, name)
                print("Removing stale " + p)
                if os.path.isdir(p):
                    shutil.rmtree(p)
                elif os.path.exists(p):
                    os.remove(p)

    # Configure (only when needed, so the generator never changes under an existing tree).
    if not os.path.exists(os.path.join(bdir, "CMakeCache.txt")):
        cfg = [cmake, "-S", ROOT, "-B", bdir]
        gen = args.generator
        if not gen and OS != "windows" and shutil.which("ninja"):
            gen = "Ninja"            # on Windows: default generator = newest Visual Studio
        if gen:
            cfg += ["-G", gen]
        if OS == "windows" and (not gen or gen.startswith("Visual Studio")):
            # 64-bit by default; EDIFICE_ARCH=Win32 for a 32-bit build.
            cfg += ["-A", os.environ.get("EDIFICE_ARCH", "x64")]
        cfg += ["-DCMAKE_BUILD_TYPE=" + args.config]
        # Extra CMake arguments, e.g. EDIFICE_CMAKE_ARGS="-DEDIFICE_LOBBY=OFF"
        extra = shlex.split(os.environ.get("EDIFICE_CMAKE_ARGS", ""))
        if args.universal:
            cfg += ["-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + MACOS_MIN]
        cfg += ["-DHK_RENDER_SOKOL=" + ("ON" if args.sokol else "OFF")]
        if args.gns:
            cfg += gns_args(args, extra)
        cfg += extra
        print("+ " + " ".join(cfg), flush=True)
        if subprocess.call(cfg) != 0:
            shutil.rmtree(bdir, ignore_errors=True)  # don't keep a half-configured tree
            die("CMake configure failed (see messages above)")

    # a tree configured before sokol_gfx became the default keeps its renderer: set it
    want = "ON" if args.sokol else "OFF"
    if cache_value(bdir, "HK_RENDER_SOKOL").upper() not in (want, "1" if args.sokol else "0", "TRUE" if args.sokol else "FALSE"):
        run([cmake, bdir, "-DHK_RENDER_SOKOL=" + want])

    # a --gns tree configured before the WebRTC bridge (or with --no-webrtc): set it,
    # vcpkg then installs libdatachannel at the next configure
    if args.gns:
        want = "ON" if args.webrtc else "OFF"
        if cache_value(bdir, "EDIFICE_WEBRTC").upper() not in (want, "1" if args.webrtc else "0"):
            run([cmake, bdir, "-DEDIFICE_WEBRTC=" + want, "-DVCPKG_MANIFEST_FEATURES=" + vcpkg_features(args)])

    run([cmake, "--build", bdir, "--config", args.config, "--parallel"])

    exe_dir, exe = locate_binary(bdir, args.config)
    print("Built: " + exe)

    if args.test:
        run([find_ctest(cmake), "--test-dir", bdir, "-C", args.config, "--output-on-failure"])
    if args.package:
        package(bdir, exe_dir, exe)
    if args.run:
        launch(exe_dir, exe, game_args)


def main_web(args):
    """--web: Emscripten's toolchain, as the CMake preset "web" (solo and offline)"""
    if args.universal or not args.sokol:
        die("--web draws with sokol_gfx on WebGL2: no --universal nor --gl", hint=False)
    cmake = find_cmake()
    emsdk = local_emsdk()
    env = web_env(emsdk)
    toolchain = os.path.join(emsdk, "upstream", "emscripten", "cmake", "Modules", "Platform", "Emscripten.cmake")
    if not os.path.isfile(toolchain):
        die("no Emscripten in %s (EMSDK)" % emsdk, hint=False)
    bdir = os.path.join(ROOT, "build", "web" + ("-debug" if args.config == "Debug" else ""))
    if args.clean and os.path.isdir(bdir):
        print("Removing " + bdir)
        shutil.rmtree(bdir)
    if not os.path.exists(os.path.join(bdir, "CMakeCache.txt")):
        if not shutil.which("ninja"):
            die("the web build needs ninja (brew install ninja, apt install ninja-build)", hint=False)
        cfg = [cmake, "-S", ROOT, "-B", bdir, "-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=" + toolchain,
               "-DCMAKE_BUILD_TYPE=" + args.config]
        cfg += shlex.split(os.environ.get("EDIFICE_CMAKE_ARGS", ""))
        print("+ " + " ".join(cfg), flush=True)
        if subprocess.call(cfg, env=env) != 0:
            shutil.rmtree(bdir, ignore_errors=True)
            die("CMake configure failed (see messages above)", hint=False)
    run([cmake, "--build", bdir, "--parallel"], env=env)
    print("Built: " + os.path.join(bdir, "index.html"))
    if args.test:
        run([find_ctest(cmake), "--test-dir", bdir, "--output-on-failure"], env=env)
    if args.package:
        package_web(bdir)
    if args.run:
        serve_web(bdir)


WEB_FILES = ("index.html", "index.js", "index.wasm", "index.data")


def package_web(bdir):
    """dist/Edifice-<version>-web.zip: the site (the files GitHub Pages serves),
    the docs and the licenses of the libraries in the binary"""
    version = project_version()
    dist = os.path.join(ROOT, "dist")
    os.makedirs(dist, exist_ok=True)
    base = "Edifice-%s-web" % version
    out = os.path.join(dist, base + ".zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in WEB_FILES:
            z.write(os.path.join(bdir, f), base + "/" + f)
        for d in ("readme.txt", "license.txt", "lgpl-3.0.txt"):
            if os.path.isfile(os.path.join(ROOT, d)):
                z.write(os.path.join(ROOT, d), base + "/" + d)
        for name, p in third_party_licenses(bdir):
            z.write(p, base + "/licenses/%s.txt" % name)
    print("Package: " + out)


def serve_web(bdir):
    """serves the site on http://127.0.0.1:8000/ (EDIFICE_WEB_PORT) until Ctrl+C:
    a page can't load its .wasm and .data from file://"""
    import functools
    import http.server
    import webbrowser

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                              **{".wasm": "application/wasm"})
    port = int(os.environ.get("EDIFICE_WEB_PORT", "8000"))
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=bdir))
    url = "http://127.0.0.1:%d/" % port
    print("Serving %s on %s (Ctrl+C stops)" % (bdir, url), flush=True)
    webbrowser.open(url)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


def locate_binary(bdir, config):
    if OS == "macos":
        app = os.path.join(bdir, "Edifice.app")
        return bdir, app
    multi = os.path.join(bdir, config)   # Visual Studio / multi-config generators
    for d in (multi, bdir):
        for name in ("Edifice.exe", "Edifice"):
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return d, p
    die("built executable not found under " + bdir, hint=False)


def launch(exe_dir, exe, game_args):
    if OS == "macos":
        cmd = [os.path.join(exe, "Contents", "MacOS", "Edifice")] + game_args
    else:
        cmd = [exe] + game_args
    print("+ " + " ".join(cmd), flush=True)
    # The game finds data/ next to the executable (or inside the .app).
    sys.exit(subprocess.call(cmd, cwd=exe_dir))


def cache_value(bdir, key):
    try:
        with open(os.path.join(bdir, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith(key + ":"):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def third_party_licenses(bdir):
    """(name, license file) of the libraries built into the game: stb_image and, but
    with --gl, sokol_gfx (vendored in src/third_party), SDL2, opus and (Windows) curl
    from FetchContent, and with --gns the vcpkg ports (GameNetworkingSockets, protobuf,
    abseil, OpenSSL...)."""
    out = [("stb_image", os.path.join(ROOT, "src", "third_party", "stb", "LICENSE"))]
    if cache_value(bdir, "HK_RENDER_SOKOL").upper() in ("ON", "1", "TRUE"):
        out.append(("sokol", os.path.join(ROOT, "src", "third_party", "sokol", "LICENSE")))
    for name, f in (("SDL2", "LICENSE.txt"), ("opus", "COPYING"), ("curl", "COPYING")):
        p = os.path.join(bdir, "_deps", name.lower() + "-src", f)
        if os.path.isfile(p):
            out.append((name, p))
    # the web build: SDL2 is Emscripten's port, in its cache, and Emscripten's
    # runtime (libc, libc++, JavaScript glue) is in the binary too
    toolchain = cache_value(bdir, "CMAKE_TOOLCHAIN_FILE")
    if toolchain.endswith("Emscripten.cmake"):
        em = os.path.normpath(os.path.join(os.path.dirname(toolchain), "..", "..", ".."))
        found = sorted(glob.glob(os.path.join(em, "cache", "ports", "sdl2", "SDL-*", "LICENSE.txt")))
        if found:
            out.append(("SDL2", found[-1]))
        if os.path.isfile(os.path.join(em, "LICENSE")):
            out.append(("emscripten", os.path.join(em, "LICENSE")))
    triplet = cache_value(bdir, "VCPKG_TARGET_TRIPLET")
    if triplet and cache_value(bdir, "EDIFICE_GNS").upper() in ("ON", "1", "TRUE"):
        share = os.path.join(bdir, "vcpkg_installed", triplet, "share")
        for p in sorted(glob.glob(os.path.join(share, "*", "copyright"))):
            port = os.path.basename(os.path.dirname(p))
            if not port.startswith("vcpkg-"):
                out.append((port, p))
    return out


def package(bdir, exe_dir, exe):
    version = project_version()
    dist = os.path.join(ROOT, "dist")
    os.makedirs(dist, exist_ok=True)
    base = "Edifice-%s-%s" % (version, OS)
    docs = [os.path.join(ROOT, f) for f in ("readme.txt", "license.txt", "lgpl-3.0.txt")]
    docs = [d for d in docs if os.path.isfile(d)]
    # licenses/<library>.txt: the third-party libraries in the binary, whose
    # licenses (zlib, BSD, Apache 2.0...) ask for their text in binary releases
    licenses = [("licenses/%s.txt" % n, p) for n, p in third_party_licenses(bdir)]

    if OS == "macos":
        out = os.path.join(dist, base + ".zip")
        if os.path.exists(out):
            os.remove(out)
        # Stage Edifice.app + docs in dist/<base>/, then zip that folder. The bundle
        # is signed by the CMake post-build step (SDL2 is linked statically); sign
        # again in case only resources changed since the last link.
        stage = os.path.join(dist, base)
        shutil.rmtree(stage, ignore_errors=True)
        os.makedirs(stage)
        app = os.path.join(stage, "Edifice.app")
        shutil.copytree(exe, app, symlinks=True)
        for d in docs:
            shutil.copy2(d, stage)
        for rel, p in licenses:
            os.makedirs(os.path.join(stage, "licenses"), exist_ok=True)
            shutil.copy2(p, os.path.join(stage, rel))
        run(["codesign", "--force", "--sign", "-", app])
        run(["codesign", "--verify", "--deep", "--strict", app])
        # No AppleDouble (__MACOSX/._*) entries: resource forks and xattrs left out.
        run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", stage, out])
        shutil.rmtree(stage)
    elif OS == "windows":
        out = os.path.join(dist, base + ".zip")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(exe, base + "/Edifice.exe")
            # --gns: GameNetworkingSockets.dll and its dependencies (copied there by
            # vcpkg) and the C++ runtime DLLs (app-local, copied by CMakeLists.txt)
            for dll in sorted(glob.glob(os.path.join(exe_dir, "*.dll"))):
                z.write(dll, base + "/" + os.path.basename(dll))
            add_tree_zip(z, DATA, base + "/data")  # data/ next to the exe, as on Linux
            for d in docs:
                z.write(d, base + "/" + os.path.basename(d))
            for rel, p in licenses:
                z.write(p, base + "/" + rel)
    else:
        out = os.path.join(dist, base + ".tar.gz")
        with tarfile.open(out, "w:gz") as t:
            t.add(exe, base + "/Edifice")
            t.add(DATA, base + "/data",
                  filter=lambda ti: None if os.path.basename(ti.name) == ".DS_Store" else ti)
            for d in docs:
                t.add(d, base + "/" + os.path.basename(d))
            for rel, p in licenses:
                t.add(p, base + "/" + rel)
    print("Package: " + out)


def add_tree_zip(z, src, prefix):
    for dirpath, _dirs, files in os.walk(src):
        for f in files:
            if f == ".DS_Store":
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            z.write(full, prefix + "/" + rel)


if __name__ == "__main__":
    main()
