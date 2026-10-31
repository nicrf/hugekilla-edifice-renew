#!/usr/bin/env python3
"""Build Edifice (HugeKilla) on macOS, Linux or Windows.

    python3 build.py [--config Debug|Release] [--clean] [--run] [--package]

Configures into build/<os>-<config>, builds, and optionally runs the game or
packages it into dist/Edifice-<version>-<os>.(zip|tar.gz). Python 3 stdlib only.
"""
import argparse
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
             "                 libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev\n"
             "  Fedora:        sudo dnf install gcc-c++ cmake ninja-build mesa-libGLU-devel libX11-devel \\\n"
             "                 libXext-devel libXrandr-devel libXcursor-devel libXi-devel alsa-lib-devel pulseaudio-libs-devel",
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


def main():
    ap = argparse.ArgumentParser(description="Build Edifice (HugeKilla).")
    ap.add_argument("--config", choices=["Debug", "Release"], default="Release")
    ap.add_argument("--clean", action="store_true", help="delete the build directory first")
    ap.add_argument("--run", action="store_true", help="launch the game after building")
    ap.add_argument("--package", action="store_true", help="create an archive in dist/")
    ap.add_argument("--generator", help="override the CMake generator (e.g. Ninja)")
    args, game_args = ap.parse_known_args()

    cmake = find_cmake()
    bdir = os.path.join(ROOT, "build", "%s-%s" % (OS, args.config.lower()))
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
        # Extra CMake arguments, e.g. EDIFICE_CMAKE_ARGS="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
        cfg += shlex.split(os.environ.get("EDIFICE_CMAKE_ARGS", ""))
        print("+ " + " ".join(cfg), flush=True)
        if subprocess.call(cfg) != 0:
            shutil.rmtree(bdir, ignore_errors=True)  # don't keep a half-configured tree
            die("CMake configure failed (see messages above)")

    run([cmake, "--build", bdir, "--config", args.config, "--parallel"])

    exe_dir, exe = locate_binary(bdir, args.config)
    print("Built: " + exe)

    if args.package:
        package(exe_dir, exe)
    if args.run:
        launch(exe_dir, exe, game_args)


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


def package(exe_dir, exe):
    version = project_version()
    dist = os.path.join(ROOT, "dist")
    os.makedirs(dist, exist_ok=True)
    base = "Edifice-%s-%s" % (version, OS)
    docs = [os.path.join(ROOT, f) for f in ("readme.txt", "license.txt", "lgpl-3.0.txt")]
    docs = [d for d in docs if os.path.isfile(d)]

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
        run(["codesign", "--force", "--sign", "-", app])
        run(["codesign", "--verify", "--deep", "--strict", app])
        # No AppleDouble (__MACOSX/._*) entries: resource forks and xattrs left out.
        run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--keepParent", stage, out])
        shutil.rmtree(stage)
    elif OS == "windows":
        out = os.path.join(dist, base + ".zip")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(exe, base + "/Edifice.exe")
            add_tree_zip(z, DATA, base + "/data")  # data/ next to the exe, as on Linux
            for d in docs:
                z.write(d, base + "/" + os.path.basename(d))
    else:
        out = os.path.join(dist, base + ".tar.gz")
        with tarfile.open(out, "w:gz") as t:
            t.add(exe, base + "/Edifice")
            t.add(DATA, base + "/data",
                  filter=lambda ti: None if os.path.basename(ti.name) == ".DS_Store" else ti)
            for d in docs:
                t.add(d, base + "/" + os.path.basename(d))
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
