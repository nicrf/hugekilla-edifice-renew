"""Internet games joined P2P (roadmap M7d), end to end on one machine:
    python3 tests/net_p2p.py <Edifice binary> <edifice-lobby binary> <gns_p2p_test binary>
Needs a build with -DEDIFICE_GNS=ON. Starts the lobby (server/) with its STUN/TURN
server, on this machine's LAN address: GameNetworkingSockets never uses loopback
addresses for ICE (neither host candidates nor TURN relays), so 127.0.0.1 would
not do. Then, twice: as is (ICE finds the direct route), and with p2p=relay on
both sides (only TURN relay candidates: the traffic goes through the lobby's
relay):
  1. tests/gns_p2p_test.cpp: two processes, each with its own lobby session, one
     listening, one connecting to its identity through the signaling relay; the
     connection must be encrypted, direct or relayed as expected
  2. the game: a host announced on the lobby (servervisibility=public), a second
     game joins it by its game code (HK_LOBBYJOIN, the join screen's Internet tab).
     It must enter the game through a P2P connection (not the direct UDP one the
     lobby's address would also allow), encrypted, direct or relayed as expected,
     and the host must see it come from a made-up 0.0.x.y address (a P2P client
     has no address the host knows).
NAT: everything runs on one machine, so there is no NAT between the two players;
what is tested is the signaling, ICE, the TURN relay and the game over them, not
the NAT traversal itself.
The games run on Linux (XDG_DATA_HOME gives each its own net.ini; they need a
display, e.g. xvfb-run), and on macOS with NET_P2P_GAMES=1 (CFFIXED_USER_HOME; the
screen must not be locked). HK_P2P_ADDRESS overrides the LAN address used."""
import json, os, platform, shutil, signal, socket, subprocess, sys, tempfile, time, urllib.request

GAME, LOBBY, P2PTEST = (os.path.abspath(a) for a in sys.argv[1:4])
LOGDIR = os.path.abspath(os.environ.get("NET_P2P_LOGS", "p2p-logs"))
URL = "http://127.0.0.1:18090"
MAC = platform.system() == "Darwin"
os.makedirs(LOGDIR, exist_ok=True)
work = tempfile.mkdtemp(prefix="edifice-p2p-")
procs = []
failures = 0


class Abort(Exception):
    pass


def check(cond, what):
    global failures
    print(("ok   " if cond else "FAIL ") + what, flush=True)
    if not cond:
        failures += 1
    return cond


def lan_address():
    if os.environ.get("HK_P2P_ADDRESS"):
        return os.environ["HK_P2P_ADDRESS"]
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))   # TEST-NET: nothing is sent, the route picks the address
        return s.getsockname()[0]
    finally:
        s.close()


def start(name, args, env=None):
    log = open(os.path.join(LOGDIR, name + ".log"), "w")
    p = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=env)
    procs.append((name, p))
    return p


def log_of(name):
    try:
        with open(os.path.join(LOGDIR, name + ".log"), errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def get(path):
    try:
        with urllib.request.urlopen(URL + path, timeout=5) as r:
            body = r.read()
            try:
                return r.status, json.loads(body or b"null")
            except ValueError:
                return r.status, body
    except urllib.error.HTTPError as e:
        return e.code, None
    except OSError:
        return 0, None


def wait_for(what, cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        r = cond()
        if r:
            return r
        time.sleep(0.25)
    check(False, what + " (timed out after %d s)" % timeout)
    return None


def stop(p):
    if p.poll() is None:
        p.send_signal(signal.SIGTERM)
        try:
            p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def pair(tag, relay):
    """gns_p2p_test: a host process and a joining one"""
    idfile = os.path.join(work, "hostid-" + tag)
    extra = ["relay"] if relay else []
    env = dict(os.environ, HK_GNS_DEBUG="1")
    host = start("p2ptest-host-" + tag, [P2PTEST, URL, "host", idfile] + extra, env)
    identity = wait_for("p2p test host has a session", lambda: os.path.exists(idfile) and open(idfile).read().strip(), 20)
    if not identity:
        stop(host)
        return
    join = start("p2ptest-join-" + tag, [P2PTEST, URL, "join", identity] + extra, env)
    try:
        join.wait(timeout=60)
    except subprocess.TimeoutExpired:
        join.kill()
        join.wait()
    try:
        host.wait(timeout=10)
    except subprocess.TimeoutExpired:
        host.kill()
        host.wait()
    out = log_of("p2ptest-join-" + tag)
    for line in out.splitlines():
        if line.startswith(("ok", "FAIL")):
            print("    " + line)
    check(join.returncode == 0, "gns_p2p_test (%s): joined P2P, %s" % (tag, "relayed" if relay else "direct"))
    check(host.returncode == 0, "gns_p2p_test (%s): the host saw it" % tag)


def game_env(name, ports, extra):
    """a game with its own user folder: net.ini with the lobby and these ports"""
    home = os.path.join(work, name)
    # Linux: $XDG_DATA_HOME/Edifice; macOS: the home folder Foundation is told about
    folder = os.path.join(home, "Library", "Application Support", "Edifice") if MAC else os.path.join(home, "Edifice")
    os.makedirs(folder)
    with open(os.path.join(folder, "net.ini"), "w", newline="\r\n") as f:
        f.write("infosendport=%d\ndatasendport=%d\nservername=P2P test %s\nname=%s\n"
                "lobby=%s\nservervisibility=public\n" % (ports, ports + 1, name, name, URL))
    env = dict(os.environ, XDG_DATA_HOME=home, CFFIXED_USER_HOME=home, SDL_AUDIODRIVER="dummy", HK_CONSOLE_LOG="1",
               ASAN_OPTIONS="detect_leaks=0", HK_SHOT="/dev/null", HK_SHOT_FRAME="999999")
    env.update(extra)
    return env


def listed(name):
    status, body = get("/v1/servers?protocol=0")
    if status != 200 or not body:
        return None
    for s in body["servers"]:
        if s["name"] == "P2P test " + name:
            return s
    return None


def games(tag, relay):
    """the game: a host on the lobby, a client joining it P2P by its code"""
    mode = {"HK_P2P": "relay"} if relay else {}
    hostname, clientname = "host" + tag, "client" + tag
    host = start("game-" + hostname, [GAME], game_env(hostname, 27020, dict(mode, HK_AUTOSTART="1")))
    game = wait_for("the host is listed (%s)" % tag, lambda: listed(hostname), 60)
    if not game:
        stop(host)
        return
    check(game.get("identity", "").startswith("str:edifice-"),
          "the host is listed with its P2P identity (%s): %s" % (tag, game.get("identity")))
    client = start("game-" + clientname, [GAME], game_env(clientname, 27040, dict(mode, HK_LOBBYJOIN=game["id"])))
    entered = wait_for("the client enters the game (%s)" % tag,
                       lambda: "console: entering game" in log_of("game-" + clientname), 60)
    clog, hlog = log_of("game-" + clientname), log_of("game-" + hostname)
    if entered:
        check(True, "the client joined by game code %s (%s)" % (game["id"], tag))
    check("gns: connecting P2P to " + game["identity"] in clog, "the client connects P2P to the listed identity (%s)" % tag)
    route = "P2P relayed (TURN)" if relay else "P2P direct"
    check(("gns: connected with %s, encrypted, %s" % (game["identity"], route)) in clog,
          "an encrypted connection, %s (%s)" % (route, tag))
    check("console: received landinfo" in clog, "the client got the land (%s)" % tag)
    check(" at 0.0." in hlog and "requests to connect" in hlog,
          "the host saw the client come P2P, from a made-up 0.0.x.y address (%s)" % tag)
    if wait_for("the host reports 2 players (%s)" % tag, lambda: (listed(hostname) or {}).get("players") == 2 or None, 30):
        check(True, "the host's renewal says 2 players (%s)" % tag)
    stop(client)
    stop(host)


try:
    address = lan_address()
    check(not address.startswith("127."), "LAN address for ICE and TURN: " + address)
    start("lobby", [LOBBY, "-http", "127.0.0.1:18090", "-signal", ":18091", "-turn", ":18478",
                    "-public-host", address, "-turn-relay-ip", address, "-rate-limit", "600"])
    if not wait_for("lobby answers", lambda: get("/healthz")[0] == 200 or None, 20):
        raise Abort
    req = urllib.request.Request(URL + "/v1/sessions", method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        sess = json.loads(r.read())
    check(sess.get("signaling") == "%s:18091" % address and sess["ice"]["turn"],
          "sessions give the signaling relay and a TURN server")

    pair("direct", False)
    pair("relay", True)
    if platform.system() == "Linux" or (MAC and os.environ.get("NET_P2P_GAMES") == "1"):
        games("direct", False)
        games("relay", True)
    else:
        print("games skipped: Linux only (NET_P2P_GAMES=1 on macOS, with an unlocked screen)")
except Abort:
    pass
finally:
    for name, p in procs:
        if p.poll() is None:
            p.kill()
        p.wait()
    for name, _ in procs:
        text = log_of(name)
        if "ERROR: AddressSanitizer" in text or "runtime error:" in text:
            check(False, "sanitizer report in %s.log" % name)
            print(text)
    shutil.rmtree(work, ignore_errors=True)

if failures:
    print("%d failure(s); logs in %s" % (failures, LOGDIR))
    sys.exit(1)
print("p2p: all tests passed")
