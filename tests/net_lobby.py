"""Internet games through the lobby (roadmap M7c), end to end on one machine:
    python3 tests/net_lobby.py <Edifice binary> <edifice-lobby binary> [<lobby_test binary>]
Starts the lobby (server/), runs lobby_test against it (the client code alone),
then a game host whose net.ini has lobby= and servervisibility=public: it must
show up in the lobby's list with its details. A second game joins it by its game
code (HK_LOBBYJOIN, the join screen's Internet tab), which the host's renewal must
report (2 players). When the host quits, its game must leave the list at once
(DELETE), long before the lobby's expiry. Linux (XDG_DATA_HOME gives each game
its own net.ini); needs a display, e.g. xvfb-run."""
import json, os, shutil, signal, subprocess, sys, tempfile, time, urllib.request

GAME, LOBBY = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
UNIT = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
LOGDIR = os.path.abspath(os.environ.get("NET_LOBBY_LOGS", "netplay-logs"))
URL = "http://127.0.0.1:18080"
TTL = 30  # seconds: a game removed sooner than this was removed by its host
os.makedirs(LOGDIR, exist_ok=True)
work = tempfile.mkdtemp(prefix="edifice-lobby-")
procs = []


class Abort(Exception):
    pass


failures = 0


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1
    return cond


def start(name, args, env=None):
    log = open(os.path.join(LOGDIR, name + ".log"), "w")
    p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=env)
    procs.append((name, p))
    return p


def get(path):
    try:
        with urllib.request.urlopen(URL + path, timeout=5) as r:
            body = r.read()
            try:
                return r.status, json.loads(body or b"null")
            except ValueError:   # /healthz: plain text
                return r.status, body
    except urllib.error.HTTPError as e:
        return e.code, None
    except OSError:
        return 0, None


def game_env(name, ports, extra):
    """a game with its own user folder: net.ini with the lobby and these ports"""
    home = os.path.join(work, name)
    os.makedirs(os.path.join(home, "Edifice"))
    with open(os.path.join(home, "Edifice", "net.ini"), "w", newline="\r\n") as f:
        f.write("infosendport=%d\ndatasendport=%d\nservername=Lobby test %s\nname=%s\n"
                "lobby=%s\nservervisibility=public\n" % (ports, ports + 1, name, name, URL))
    env = dict(os.environ, XDG_DATA_HOME=home, SDL_AUDIODRIVER="dummy",
               ASAN_OPTIONS="detect_leaks=0", HK_SHOT="/dev/null", HK_SHOT_FRAME="999999")
    env.update(extra)
    return env


def wait_for(what, cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        r = cond()
        if r:
            return r
        time.sleep(0.25)
    check(False, what + " (timed out after %d s)" % timeout)
    return None


def listed(name):
    status, body = get("/v1/servers?protocol=0")
    if status != 200 or not body:
        return None
    for s in body["servers"]:
        if s["name"] == "Lobby test " + name:
            return s
    return None


try:
    start("lobby", [LOBBY, "-http", "127.0.0.1:18080", "-signal", "127.0.0.1:18081",
                    "-turn", "127.0.0.1:18478", "-ttl", "%ds" % TTL, "-rate-limit", "600"])
    if not wait_for("lobby answers", lambda: get("/healthz")[0] == 200 or None, 20):
        raise Abort

    if UNIT:
        r = subprocess.run([UNIT], env=dict(os.environ, HK_LOBBY_URL=URL), capture_output=True, text=True)
        print(r.stdout, end="")
        check(r.returncode == 0, "lobby_test against the lobby")

    # the host announces itself
    host = start("host", [GAME], game_env("host", 27020, {"HK_AUTOSTART": "1"}))
    game = wait_for("the host is listed", lambda: listed("host"), 60)
    if not game:
        raise Abort
    print("listed:", game)
    check(game["address"] == "127.0.0.1" and game["port"] == 27020, "address and info port of the host")
    check(game["players"] == 1 and game["maxplayers"] == 32 and game["mode"] == 0, "players, max players, mode")
    check(game["visibility"] == "public" and game["protocol"] > 0 and game["version"], "visibility, protocol, version")
    check(game["identity"].startswith("str:edifice-"), "the host's session identity is announced")
    code = game["id"]
    check(len(code) == 6, "a 6-character game code: " + code)
    status, one = get("/v1/servers/" + code.lower())
    check(status == 200 and one["id"] == code, "found by its code in lower case")

    # a second game joins it by its code, typed like a player would
    typed = code[:3].lower() + "-" + code[3:]
    client = start("client", [GAME], game_env("client", 27040, {"HK_LOBBYJOIN": typed}))
    wait_for("the host reports the joined player",
             lambda: (listed("host") or {}).get("players") == 2 or None, 60)

    # the host quits: its game leaves the list right away
    client.send_signal(signal.SIGTERM)
    client.wait(timeout=20)
    host.send_signal(signal.SIGTERM)
    t = time.time()
    try:
        host.wait(timeout=20)
    except subprocess.TimeoutExpired:
        check(False, "the host quits on SIGTERM")
    quit_time = time.time() - t
    check(listed("host") is None, "removed from the list when the host quit (%.1f s, expiry %d s)" % (quit_time, TTL))
    check(get("/v1/servers/" + code)[0] == 404, "its code no longer answers")
except Abort:
    pass
finally:
    for name, p in procs:
        if p.poll() is None:
            p.kill()
        p.wait()
    for name, _ in procs:
        with open(os.path.join(LOGDIR, name + ".log"), errors="replace") as f:
            text = f.read()
        if "ERROR: AddressSanitizer" in text or "runtime error:" in text:
            check(False, "sanitizer report in %s.log" % name)
            print(text)
    shutil.rmtree(work, ignore_errors=True)

if failures:
    print("%d failure(s); logs in %s" % (failures, LOGDIR))
    sys.exit(1)
print("lobby: all tests passed")
