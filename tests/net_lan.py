"""Two players behind the same router (same public address), end to end on one
machine:
    python3 tests/net_lan.py <Edifice binary> <edifice-lobby binary> [p2p]
A local lobby (-trust-proxy) behind two tiny reverse proxies: through :18191
every request looks like it comes from 203.0.113.7 (the "home" public address,
unreachable: a router that doesn't loop back), through :18192 from 198.51.100.9
(another network). The host announces through :18191 with its LAN address. A
player through :18191 must be given that address and join through it; one
through :18192 must see it neither in the list nor by game code, and can't
join. p2p: net.ini p2p= of the games (default 0; 1 with a GameNetworkingSockets
build: P2P is tried beside the LAN address). Needs a private IPv4 address on
the machine and a display (each game has its own HK_USERDIR)."""
import http.server, json, os, shutil, socketserver, subprocess, sys, tempfile, threading, time, urllib.request

GAME, LOBBY = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
P2P = sys.argv[3] if len(sys.argv) > 3 else "0"
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
LOBBYURL = "http://127.0.0.1:18190"
work = tempfile.mkdtemp(prefix="edifice-lan-")
procs = []
fails = 0


def check(c, what):
    global fails
    print(("ok   " if c else "FAIL ") + what, flush=True)
    fails += not c


def proxy(port, fwd):
    class H(http.server.BaseHTTPRequestHandler):
        def do(self):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n) if n else None
            req = urllib.request.Request(LOBBYURL + self.path, data=body, method=self.command)
            for k, v in self.headers.items():
                if k.lower() not in ("host", "content-length", "x-forwarded-for", "connection"):
                    req.add_header(k, v)
            req.add_header("X-Forwarded-For", fwd)
            try:
                with urllib.request.urlopen(req, timeout=10) as r:
                    status, data, ctype = r.status, r.read(), r.headers.get("Content-Type", "")
            except urllib.error.HTTPError as e:
                status, data, ctype = e.code, e.read(), e.headers.get("Content-Type", "")
            except OSError:   # the lobby is gone (end of the test)
                status, data, ctype = 502, b"", ""
            self.send_response(status)
            if ctype:
                self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        do_GET = do_POST = do_PUT = do_DELETE = do

        def log_message(self, *a):
            pass
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    s = socketserver.ThreadingTCPServer(("127.0.0.1", port), H)
    s.daemon_threads = True
    threading.Thread(target=s.serve_forever, daemon=True).start()


def get(port, path):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d%s" % (port, path), timeout=5) as r:
            return json.loads(r.read())
    except Exception as e:
        return None


def game(name, lobbyport, ports, extra):
    d = os.path.join(work, name) + "/"
    os.makedirs(d)
    for f in os.listdir(DATA):
        if f.endswith(".ini"):
            shutil.copy(os.path.join(DATA, f), d)
    with open(d + "net.ini", "w", newline="\r\n") as f:
        f.write("infosendport=%d\ndatasendport=%d\nservername=LAN test %s\nname=%s\nmaxplayers=8\n"
                "lobby=http://127.0.0.1:%d\nservervisibility=public\np2p=%s\n" % (ports, ports + 1, name, name, lobbyport, P2P))
    env = dict(os.environ, HK_USERDIR=d, HK_CONSOLE_LOG="1", SDL_AUDIODRIVER="dummy",
               HK_SHOT="/dev/null", HK_SHOT_FRAME="999999")
    env.update(extra)
    log = open(os.path.join(work, name + ".log"), "w")
    p = subprocess.Popen([GAME], stdout=log, stderr=subprocess.STDOUT, env=env)
    procs.append(p)
    return p


def wait(cond, t):
    end = time.time() + t
    while time.time() < end:
        r = cond()
        if r:
            return r
        time.sleep(0.25)
    return None


def log(name):
    return open(os.path.join(work, name + ".log")).read()


try:
    procs.append(subprocess.Popen([LOBBY, "-http", "127.0.0.1:18190", "-signal", "127.0.0.1:18193", "-turn", "127.0.0.1:18579",
                                   "-trust-proxy", "-rate-limit", "600"],
                                  stdout=open(os.path.join(work, "lobby.log"), "w"), stderr=subprocess.STDOUT))
    proxy(18191, "203.0.113.7")
    proxy(18192, "198.51.100.9")
    wait(lambda: get(18191, "/v1/servers?protocol=0") is not None, 10)

    game("host", 18191, 27120, {"HK_AUTOSTART": "1"})
    listed = wait(lambda: [s for s in (get(18191, "/v1/servers?protocol=0") or {}).get("servers", [])], 30)
    check(bool(listed), "host listed")
    home = listed[0]
    print("seen from home:", json.dumps(home))
    check(home["address"] == "203.0.113.7", "listed with the public address the lobby saw")
    check(bool(home.get("lan")), "same public address: LAN addresses given")
    other = get(18192, "/v1/servers?protocol=0")["servers"][0]
    print("seen from elsewhere:", json.dumps(other))
    check("lan" not in other, "other public address: no LAN addresses in the list")
    bycode = get(18192, "/v1/servers/" + home["id"])
    check(bycode is not None and "lan" not in bycode, "other public address: none by game code either")

    # joiner B first (another network): without P2P it can't get in (the public
    # address is unreachable); with P2P it does (one machine: no NAT in the way)
    game("away", 18192, 27160, {"HK_LOBBYJOIN": home["id"]})
    if P2P == "0":
        time.sleep(12)
        check("away has joined" not in log("host"), "joiner from another network not in")
    else:
        check(bool(wait(lambda: "away has joined" in log("host"), 30)), "joiner from another network in, P2P")
        check("connecting P2P" in log("away"), "...through its P2P connection")
    check("answered on the LAN" not in log("away"), "joiner from another network never tried the LAN")
    procs[-1].terminate()

    # joiner A (same public address): joins through the LAN address
    game("home", 18191, 27140, {"HK_LOBBYJOIN": home["id"]})
    check(bool(wait(lambda: "home has joined" in log("host"), 30)), "joiner from the same public address got in")
    check("answered on the LAN at" in log("home"), "joined through the LAN address")
finally:
    for p in procs:
        if p.poll() is None:
            p.terminate()
    time.sleep(1)
    for p in procs:
        if p.poll() is None:
            p.kill()
    for n in ("host", "home", "away"):
        try:
            print("---- %s (lobby/join lines)" % n)
            for line in log(n).splitlines():
                if any(k in line for k in ("lobby", "LAN", "join", "Contacting", "No answer", "connection", "gns", "P2P")):
                    print("   ", line)
        except OSError:
            pass
    print("logs in", work)
print("FAILURES: %d" % fails)
sys.exit(1 if fails else 0)
