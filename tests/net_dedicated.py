"""The dedicated server (src/platform/dedicated.h, server/README.md), end to end:
    python3 tests/net_dedicated.py <edifice-server | Edifice> [--game <Edifice>] [--lobby <edifice-lobby>]
    python3 tests/net_dedicated.py --docker <image> [--lobby <edifice-lobby>]
Starts the server on local ports (HK_TEST_PORT, 37720 by default) in a temporary
settings folder, with its flags: name, max players, visibility lan and no lobby, or
public on a local lobby (--lobby: server/ built, its STUN/TURN too, on 127.0.0.1
only). Then:
 - --healthcheck answers, INFO has the name and nobody in the game (the server isn't
   a player), the lobby lists the game;
 - test clients (tests/netclient.py) join: slots 1 and up, never anyone in slot 0 of
   the snapshots; --max-players is kept (the one past it is refused);
 - a client walks (the server moves it from its commands), another one shoots it (the
   server's hits), a console command on stdin reaches the players (say);
 - a real game client (--game, HK_JOIN) joins and plays: the others see it;
 - SIGTERM (docker stop): the players are told (the chat line, EXIT), the game leaves
   the lobby at once, the server exits with 0.
Its idle CPU is printed. The game as the server is run with --dedicated. Needs a
display for --game (Linux: xvfb-run). The server's log: netplay-logs/net_dedicated.log
(NET_DEDICATED_LOGS)."""
import json, os, re, shutil, signal, subprocess, sys, tempfile, time, urllib.request

PORT = int(os.environ.setdefault("HK_TEST_PORT", "37720"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from netclient import *   # noqa: E402 (after HK_TEST_PORT)

args = sys.argv[1:]


def option(name):
    if name in args:
        i = args.index(name)
        value = args[i + 1]
        del args[i:i + 2]
        return value
    return None


GAME = option("--game")
LOBBY = option("--lobby")
IMAGE = option("--docker")
SERVER = os.path.abspath(args[0]) if args else None
if not SERVER and not IMAGE:
    print(__doc__)
    sys.exit(2)
LOGDIR = os.path.abspath(os.environ.get("NET_DEDICATED_LOGS", "netplay-logs"))
os.makedirs(LOGDIR, exist_ok=True)
LOG = os.path.join(LOGDIR, "net_dedicated.log")
LOBBYPORT = PORT + 100
LOBBYURL = "http://127.0.0.1:%d" % LOBBYPORT
NAME = "CI dedicated %d" % PORT
MAXPLAYERS = 3
CONTAINER = "edifice-server-test-%d" % PORT
work = tempfile.mkdtemp(prefix="edifice-dedicated-")
procs = []
game = Game()
check = game.check


def lobby_get(path):
    try:
        with urllib.request.urlopen(LOBBYURL + path, timeout=5) as r:
            body = r.read()
            try:
                return r.status, json.loads(body or b"null")
            except ValueError:   # /healthz: plain text
                return r.status, body
    except (OSError, ValueError):
        return 0, None


def listed():
    status, body = lobby_get("/v1/servers?protocol=0")
    if status != 200 or not body:
        return None
    return next((s for s in body["servers"] if s["name"] == NAME), None)


def wait_for(cond, timeout, step=0.25, pump=False):
    end = time.time() + timeout
    while time.time() < end:
        r = cond()
        if r:
            return r
        if pump:
            game.pump(step)
        else:
            time.sleep(step)
    return None


def cpu_seconds(pid):
    """the process's CPU time (ps), None if unknown"""
    try:
        out = subprocess.run(["ps", "-o", "time=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    except OSError:
        return None
    m = re.match(r"(?:(\d+)-)?(?:(\d+):)?(\d+):(\d+(?:\.\d+)?)$", out)
    if not m:
        return None
    d, h, mi, s = m.groups()
    return int(d or 0) * 86400 + int(h or 0) * 3600 + int(mi) * 60 + float(s)


def log_text():
    try:
        return open(LOG, errors="replace").read()
    except OSError:
        return ""


def healthy():
    if IMAGE:
        cmd = ["docker", "exec", CONTAINER, "/opt/edifice/edifice-server", "--healthcheck"]
        env = None
    else:
        cmd = [SERVER, "--healthcheck"] + ([] if os.path.basename(SERVER).startswith("edifice-server") else ["--dedicated"])
        env = dict(os.environ, HK_PORT=str(PORT))
    return subprocess.run(cmd, env=env, capture_output=True).returncode == 0


class Abort(Exception):
    pass


server = None
try:
    if LOBBY:
        lobby = subprocess.Popen([os.path.abspath(LOBBY), "-http", "127.0.0.1:%d" % LOBBYPORT, "-signal", "off",
                                  "-turn", "127.0.0.1:%d" % (LOBBYPORT + 1), "-public-host", "127.0.0.1",
                                  "-turn-relay-ip", "127.0.0.1", "-rate-limit", "600", "-allow-private"],
                                 stdout=open(os.path.join(LOGDIR, "net_dedicated-lobby.log"), "w"),
                                 stderr=subprocess.STDOUT)
        procs.append(lobby)
        if not check(wait_for(lambda: lobby_get("/healthz")[0] == 200, 20),
                     "the local lobby answers"):
            raise Abort
    flags = ["--port", str(PORT), "--name", NAME, "--max-players", str(MAXPLAYERS), "--seed", "7",
             "--visibility", "public" if LOBBY else "lan", "--lobby", LOBBYURL if LOBBY else "",
             "--exec", "timelimit 30;capturelimit 3"]
    # the test clients ask where to stand (HK_TESTPLACE), join as builds without the
    # rocks and trees (props 0), and the anti-cheat speaks on stderr
    testenv = {"HK_TESTPLACE": "1", "HK_ANTICHEAT_LOG": "1", "HK_AC_VARS": "props=0"}
    if IMAGE:
        net = ["--network", "host"] if sys.platform.startswith("linux") else \
            ["-p", "%d-%d:%d-%d/udp" % (PORT, PORT + 2 + MAXPLAYERS + 2, PORT, PORT + 2 + MAXPLAYERS + 2)]
        cmd = ["docker", "run", "--rm", "-i", "--name", CONTAINER] + net + \
            sum((["-e", "%s=%s" % kv] for kv in testenv.items()), []) + [IMAGE] + flags
        env = None
    else:
        cmd = [SERVER] + ([] if os.path.basename(SERVER).startswith("edifice-server") else ["--dedicated"]) + flags
        env = dict(os.environ, HK_USERDIR=work + "/", **testenv)
        for k in ("HK_AUTOSTART", "HK_JOIN", "HK_DEDICATED", "HK_PORT", "HK_LOBBY", "HK_VISIBILITY", "HK_MAP",
                  "HK_MAPFILE", "HK_CONSOLE_RUN", "HK_SERVER_NAME", "HK_MAX_PLAYERS", "HK_MODE", "HK_SEED"):
            env.pop(k, None)
    print("+ " + " ".join(cmd), flush=True)
    server = subprocess.Popen(cmd, env=env, stdin=subprocess.PIPE, stdout=open(LOG, "wb"), stderr=subprocess.STDOUT)
    procs.append(server)

    def console(line):
        server.stdin.write((line + "\n").encode())
        server.stdin.flush()

    # ---- up ----
    check(wait_for(healthy, 60, 0.5), "--healthcheck answers")
    trailer, caps, info = server_info()
    name = info[2:2 + info[1]].decode(errors="replace")
    t = info[2 + info[1] + 1:]
    players, maxplayers = t[11 + t[10]], t[11 + t[10] + 1]
    check(name == NAME, "INFO has its name (%r)" % name)
    check(players == 0 and maxplayers == MAXPLAYERS, "nobody in the game, %d at most (INFO %d/%d)" % (
        MAXPLAYERS, players, maxplayers))
    if LOBBY:
        s = wait_for(listed, 20)
        check(s and s["players"] == 0 and s["maxplayers"] == MAXPLAYERS,
              "listed on the lobby, 0/%d (%s)" % (MAXPLAYERS, s and "%d/%d" % (s["players"], s["maxplayers"])))

    # ---- idle ----
    if not IMAGE:
        c0, t0 = cpu_seconds(server.pid), time.time()
        time.sleep(5)
        c1, t1 = cpu_seconds(server.pid), time.time()
        if c0 is not None and c1 is not None:
            print("idle CPU: %.1f%% of a core" % ((c1 - c0) / (t1 - t0) * 100), flush=True)

    # ---- players ----
    a, b, c = Client("Walker"), Client("Shooter"), Client("Third")
    for cl in (a, b, c):
        if not check(cl.join(trailer, game.clients), "%s joins" % cl.name.decode()):
            raise Abort
        game.add(cl)
    check([cl.slot for cl in (a, b, c)] == [1, 2, 3], "slots 1 to 3 (%s): none taken by the server" % (
        [cl.slot for cl in (a, b, c)]))
    full = Client("Fourth")
    check(not full.join(trailer, game.clients), "the one past --max-players %d is refused" % MAXPLAYERS)
    game.pump(1.0)
    _, _, info = server_info()
    t = info[2 + info[1] + 1:]
    check(t[11 + t[10]] == 3, "INFO counts the 3 players (%d)" % t[11 + t[10]])
    if LOBBY:
        s = wait_for(lambda: (listed() or {}).get("players") == 3 and listed(), 20, 0.5, pump=True)
        check(s, "the lobby is told: 3 players")

    teams = {}
    for cl in game.clients:
        teams.update(cl.teams)
    print("teams:", teams)
    check(teams.get(0, 0) == 0, "the server is in no team (%s)" % teams.get(0))
    check(teams.get(1) in (1, 2) and teams.get(2) in (1, 2) and teams[1] != teams[2],
          "the first two are on the two teams")

    # ---- walking ----
    a.place(150, 120, 150)
    b.place(152, 120, 150)
    c.place(330, 120, 330)
    game.settle(game.clients, 6)
    check(0 not in a.others() and 0 not in b.others(), "nobody in slot 0 of the snapshots (%s)" % sorted(a.others()))
    check(b.slot in a.others() and c.slot in a.others(), "each sees the others in them")
    if a.own:
        start = a.own[0:3]
        a.keys = MOVEKEY_UP
        game.pump(2.0)
        a.keys = 0
        game.pump(0.5)
        moved = wrapdist(start, a.own[0:3]) if a.own else 0
        check(moved > 3, "the server walks a client from its commands (%.1f units in 2 s)" % moved)
    else:
        check(False, "the walker has its state")

    # ---- a hit ----
    a.place(150, 120, 150)
    b.place(152, 120, 150)
    game.settle((a, b), 6)
    v = b.others().get(a.slot)
    mark = a.mark()
    if v and b.own:
        for k in range(4):
            b.fire(aim(b.own[0:3], v))
            game.pump(0.15)
    game.pump(0.6)
    hits = a.simples(mark, SERVERHIT)
    check(hits and all(by == b.slot for by, life in hits) and hits[-1][1] < 100,
          "the shooter hits the walker: the server says so (SERVERHIT by, life: %s)" % hits)

    # ---- the console on stdin ----
    mark = c.mark()
    console("say hello from the console")
    console("status")
    game.pump(1.0)
    said = [m for m in c.since(mark) if m and m[0] == CHATMESSAGE and b"hello from the console" in m]
    check(said, "a console command on stdin reaches the players (say)")
    check(wait_for(lambda: "Walker" in log_text() and "3/3 players" in log_text(), 5),
          "status lists the players in the log")

    # ---- a real game client ----
    if GAME:
        c.leave()
        game.clients.remove(c)
        game.pump(1.5)
        home = os.path.join(work, "game")
        os.makedirs(home)
        data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
        for f in ("control", "net", "sv"):
            shutil.copy(os.path.join(data, f + ".ini"), home)
        lines = open(os.path.join(home, "net.ini"), "rb").read().decode().split("\r\n")
        fix = {"infosendport": str(PORT), "datasendport": str(PORT + 1), "lobby": "", "servervisibility": "lan",
               "name": "RealGame"}
        lines = [k + "=" + fix[k] if k in fix else l for l, k in ((l, l.split("=")[0]) for l in lines)]
        open(os.path.join(home, "net.ini"), "wb").write("\r\n".join(lines).encode())
        # a client that joins waits its 5 s to respawn, then fire spawns it: the trigger
        # of the scripted pad (HK_PADTEST), pulled every second, at 60 frames a second
        pulls = ";".join("%d:down:righttrigger;%d:up:righttrigger" % (f, f + 10) for f in range(120, 7200, 60))
        genv = dict(os.environ, HK_USERDIR=home + "/", HK_JOIN="127.0.0.1", SDL_AUDIODRIVER="dummy",
                    HK_SHOT="/dev/null", HK_SHOT_FRAME="99999999", HK_MAXFPS="60", HK_PADTEST=pulls,
                    ASAN_OPTIONS="detect_leaks=0")
        for k in ("HK_AUTOSTART", "HK_AC_VARS", "HK_TESTPLACE", "HK_DEDICATED"):
            genv.pop(k, None)
        gproc = subprocess.Popen([os.path.abspath(GAME)], env=genv, stdout=open(os.path.join(LOGDIR, "net_dedicated-game.log"), "wb"),
                                 stderr=subprocess.STDOUT)
        procs.append(gproc)
        joined = wait_for(lambda: "RealGame has connected" in log_text(), 60, 0.25, pump=True)
        check(joined, "a game client (HK_JOIN) joins the dedicated server")
        slot = int(re.search(r"RealGame has connected \(player (\d+)\)", log_text()).group(1)) if joined else None
        playing = wait_for(lambda: slot in a.others() and a.others()[slot][3] == 10, 60, 0.25, pump=True)
        check(playing, "and plays: the others see it in the game (slot %s)" % slot)
        if playing:
            p0 = a.others()[slot][0:3]
            game.pump(2.0)
            check(gproc.poll() is None and slot in a.others(), "it stays (%.1f units from where it spawned)" % (
                wrapdist(p0, a.others()[slot][0:3]) if slot in a.others() else -1))
        gproc.terminate()
        try:
            gproc.wait(10)
        except subprocess.TimeoutExpired:
            gproc.kill()

    # ---- SIGTERM ----
    marks = {cl.name: cl.mark() for cl in game.clients}
    stopped = time.time()
    if IMAGE:
        subprocess.run(["docker", "stop", "-t", "10", CONTAINER], capture_output=True)
    else:
        server.send_signal(signal.SIGTERM)
    code = None
    try:
        end = time.time() + 12
        while time.time() < end and server.poll() is None:
            for cl in game.clients:
                cl.drain()
            time.sleep(0.02)
        code = server.wait(1)
    except subprocess.TimeoutExpired:
        pass
    for cl in game.clients:
        cl.drain()
    check(code == 0, "SIGTERM: the server exits with 0 (%s, in %.1f s)" % (code, time.time() - stopped))
    told = all(any(is_simple(m, EXIT) for m in cl.since(marks[cl.name])) for cl in game.clients)
    bye = all(any(m and m[0] == CHATMESSAGE and b"shutting down" in m for m in cl.since(marks[cl.name]))
              for cl in game.clients)
    check(told, "every player gets EXIT")
    check(bye, "and the chat line saying so")
    check("Stopped" in log_text(), "its log ends with Stopped")
    if LOBBY:
        check(listed() is None, "the game left the lobby at once (DELETE)")
    events = [l for l in log_text().splitlines() if l.startswith("anticheat: ") and
              re.match(r"anticheat: (kill|death|fire|ammo|build|state|ip|origin|cmd):", l)]
    check(not events, "no anti-cheat event (%s)" % (events[:3] or "none"))
except Abort:
    pass
finally:
    for p in procs:
        if p.poll() is None:
            p.kill()
            p.wait()
    if IMAGE:
        subprocess.run(["docker", "rm", "-f", CONTAINER], capture_output=True)
    shutil.rmtree(work, ignore_errors=True)
if not game.ok:
    print("-- server log (tail):")
    print("\n".join(log_text().splitlines()[-40:]))
print("PASS" if game.ok else "FAILED")
sys.exit(0 if game.ok else 1)
