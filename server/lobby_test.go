package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/pion/turn/v4"
)

func announcement(name string) Announcement {
	return Announcement{Name: name, Port: 27020, Version: "0.3.0", Protocol: 3, Players: 2, MaxPlayers: 32}
}

func TestRegistryLifecycle(t *testing.T) {
	r := NewRegistry(time.Minute, 2, 100)
	clock := time.Unix(1000, 0)
	r.now = func() time.Time { return clock }

	id, token, err := r.Register("1.2.3.4", announcement("Fort"))
	if err != nil {
		t.Fatal(err)
	}
	if list := r.List(3); len(list) != 1 || list[0].Address != "1.2.3.4" || list[0].Name != "Fort" {
		t.Fatalf("list = %+v", list)
	}
	if list := r.List(4); len(list) != 0 {
		t.Fatalf("protocol filter: %+v", list)
	}
	if err := r.Refresh(id, "wrong", announcement("Fort")); err != ErrBadToken {
		t.Fatalf("refresh with a bad token: %v", err)
	}
	clock = clock.Add(50 * time.Second)
	if err := r.Refresh(id, token, announcement("Fort 2")); err != nil {
		t.Fatal(err)
	}
	clock = clock.Add(50 * time.Second) // 50 s after the refresh: still there
	if list := r.List(0); len(list) != 1 || list[0].Name != "Fort 2" {
		t.Fatalf("after refresh: %+v", list)
	}
	clock = clock.Add(20 * time.Second) // 70 s without refresh: gone
	if list := r.List(0); len(list) != 0 {
		t.Fatalf("not expired: %+v", list)
	}
}

func TestRegistryLimitsAndValidation(t *testing.T) {
	r := NewRegistry(time.Minute, 2, 100)
	for i := 0; i < 2; i++ {
		if _, _, err := r.Register("5.5.5.5", announcement("g")); err != nil {
			t.Fatal(err)
		}
	}
	if _, _, err := r.Register("5.5.5.5", announcement("g")); err != ErrTooMany {
		t.Fatalf("third game from one IP: %v", err)
	}
	bad := announcement("x")
	bad.Port = 0
	if _, _, err := r.Register("6.6.6.6", bad); err != ErrBadRequest {
		t.Fatalf("port 0: %v", err)
	}
	weird := announcement("café \x01<b>")
	id, _, err := r.Register("6.6.6.6", weird)
	if err != nil {
		t.Fatal(err)
	}
	if s, _ := r.Get(id); s.Name != "caf <b>" {
		t.Fatalf("name not cleaned to printable ASCII: %q", s.Name)
	}
}

func TestPrivateGamesAreNotListed(t *testing.T) {
	r := NewRegistry(time.Minute, 4, 100)
	a := announcement("secret")
	a.Visibility = "private"
	id, _, err := r.Register("7.7.7.7", a)
	if err != nil {
		t.Fatal(err)
	}
	if len(r.List(0)) != 0 {
		t.Fatal("private game listed")
	}
	if s, ok := r.Get(id); !ok || s.Name != "secret" {
		t.Fatal("private game not reachable by id")
	}
}

func TestGameCodes(t *testing.T) {
	r := NewRegistry(time.Minute, 4, 100)
	id, _, err := r.Register("8.8.8.8", announcement("coded"))
	if err != nil {
		t.Fatal(err)
	}
	if len(id) != 6 || strings.Trim(id, codeAlphabet) != "" {
		t.Fatalf("game code %q", id)
	}
	typed := strings.ToLower(id[:3]) + "-" + id[3:] + " "
	if s, ok := r.Get(typed); !ok || s.ID != id {
		t.Fatalf("lookup of %q failed", typed)
	}
}

func TestTURNCredentialsMatchPion(t *testing.T) {
	secret := "s3cret"
	s := NewSessions(time.Hour, 10, []string{"stun:x:3478"}, []string{"turn:x:3478?transport=udp"}, secret)
	sess, err := s.Create()
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(sess.Identity, "str:edifice-") || len(sess.ICE.TURN) != 1 {
		t.Fatalf("session = %+v", sess)
	}
	cred := sess.ICE.TURN[0]
	key, ok := turn.LongTermTURNRESTAuthHandler(secret, nil)(cred.Username, "edifice", nil)
	if !ok {
		t.Fatal("pion rejected the credentials")
	}
	if !bytes.Equal(key, turn.GenerateAuthKey(cred.Username, "edifice", cred.Credential)) {
		t.Fatal("password does not match what pion expects")
	}
	if !s.Check(sess.Identity, sess.Token) || s.Check(sess.Identity, "nope") {
		t.Fatal("session token check")
	}
}

func TestSignalingRelay(t *testing.T) {
	sessions := NewSessions(time.Hour, 10, nil, nil, "x")
	alice, _ := sessions.Create()
	bob, _ := sessions.Create()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go NewSignaling(sessions).Serve(l)

	dial := func(hello string) (net.Conn, *bufio.Reader) {
		c, err := net.Dial("tcp", l.Addr().String())
		if err != nil {
			t.Fatal(err)
		}
		c.Write([]byte(hello + "\n"))
		c.SetReadDeadline(time.Now().Add(3 * time.Second))
		return c, bufio.NewReader(c)
	}
	ca, _ := dial(alice.Identity + " " + alice.Token)
	defer ca.Close()
	cb, rb := dial(bob.Identity + " " + bob.Token)
	defer cb.Close()
	time.Sleep(100 * time.Millisecond) // let both register

	ca.Write([]byte(bob.Identity + " aGVsbG8=\n"))
	line, err := rb.ReadString('\n')
	if err != nil {
		t.Fatal(err)
	}
	if line != alice.Identity+" aGVsbG8=\n" {
		t.Fatalf("bob got %q", line)
	}

	// someone pretending to be alice without her token is turned away
	cx, rx := dial(alice.Identity + " " + bob.Token)
	defer cx.Close()
	if line, _ := rx.ReadString('\n'); line != "ERROR unauthorized\n" {
		t.Fatalf("impostor got %q", line)
	}
}

func TestHTTPAPI(t *testing.T) {
	api := &API{registry: NewRegistry(time.Minute, 4, 100), sessions: NewSessions(time.Hour, 10, nil, nil, "x"),
		limiter: newRateLimiter(600, 100), refresh: 20 * time.Second}
	ts := httptest.NewServer(api)
	defer ts.Close()

	body, _ := json.Marshal(announcement("HTTP fort"))
	resp, err := http.Post(ts.URL+"/v1/servers", "application/json", bytes.NewReader(body))
	if err != nil || resp.StatusCode != http.StatusCreated {
		t.Fatalf("announce: %v %v", err, resp.Status)
	}
	var created struct {
		ID, Token string
		Refresh   int `json:"refresh_seconds"`
	}
	json.NewDecoder(resp.Body).Decode(&created)
	if created.ID == "" || created.Token == "" || created.Refresh != 20 {
		t.Fatalf("created = %+v", created)
	}

	resp, _ = http.Get(ts.URL + "/v1/servers?protocol=3")
	var list struct{ Servers []Server }
	json.NewDecoder(resp.Body).Decode(&list)
	if len(list.Servers) != 1 || list.Servers[0].Address != "127.0.0.1" || list.Servers[0].Name != "HTTP fort" {
		t.Fatalf("list = %+v", list)
	}

	req, _ := http.NewRequest(http.MethodDelete, ts.URL+"/v1/servers/"+created.ID, nil)
	req.Header.Set("Authorization", "Bearer wrong")
	if resp, _ := http.DefaultClient.Do(req); resp.StatusCode != http.StatusForbidden {
		t.Fatalf("delete with a bad token: %v", resp.Status)
	}
	req.Header.Set("Authorization", "Bearer "+created.Token)
	if resp, _ := http.DefaultClient.Do(req); resp.StatusCode != http.StatusNoContent {
		t.Fatalf("delete: %v", resp.Status)
	}

	resp, _ = http.Post(ts.URL+"/v1/sessions", "application/json", nil)
	var sess Session
	json.NewDecoder(resp.Body).Decode(&sess)
	if resp.StatusCode != http.StatusCreated || sess.Identity == "" || sess.Token == "" {
		t.Fatalf("session: %v %+v", resp.Status, sess)
	}

	// an identity is only listed with the token of its session
	announce := func(identity, session string) string {
		a := announcement("P2P fort")
		a.Identity, a.Session = identity, session
		body, _ := json.Marshal(a)
		resp, err := http.Post(ts.URL+"/v1/servers", "application/json", bytes.NewReader(body))
		if err != nil || resp.StatusCode != http.StatusCreated {
			t.Fatalf("announce: %v %v", err, resp.Status)
		}
		var c struct{ ID string }
		json.NewDecoder(resp.Body).Decode(&c)
		resp, _ = http.Get(ts.URL + "/v1/servers/" + c.ID)
		raw, _ := io.ReadAll(resp.Body)
		if strings.Contains(string(raw), session) && session != "" {
			t.Fatalf("the session token is shown: %s", raw)
		}
		var s Server
		json.Unmarshal(raw, &s)
		return s.Identity
	}
	if got := announce(sess.Identity, sess.Token); got != sess.Identity {
		t.Fatalf("own identity not listed: %q", got)
	}
	if got := announce(sess.Identity, "wrong"); got != "" {
		t.Fatalf("someone else's identity listed: %q", got)
	}
	if got := announce(sess.Identity, ""); got != "" {
		t.Fatalf("identity listed without proof: %q", got)
	}
}

func TestSessionSignalingAddress(t *testing.T) {
	s := NewSessions(time.Hour, 10, nil, nil, "x")
	if sess, _ := s.Create(); sess.Signaling != "" {
		t.Fatalf("signaling = %q", sess.Signaling)
	}
	s.signaling = "lobby.example.org:10000"
	if sess, _ := s.Create(); sess.Signaling != "lobby.example.org:10000" {
		t.Fatalf("signaling = %q", sess.Signaling)
	}
}

func TestRateLimiter(t *testing.T) {
	l := newRateLimiter(60, 3) // one per second, burst 3
	clock := time.Unix(0, 0)
	l.now = func() time.Time { return clock }
	for i := 0; i < 3; i++ {
		if !l.allow("ip") {
			t.Fatalf("request %d refused", i)
		}
	}
	if l.allow("ip") {
		t.Fatal("burst exceeded but allowed")
	}
	clock = clock.Add(time.Second)
	if !l.allow("ip") {
		t.Fatal("not refilled after a second")
	}
}

func TestTURNAllocationWithSessionCredentials(t *testing.T) {
	secret := "relay-secret"
	server, err := StartTURN("127.0.0.1:34780", "127.0.0.1", "edifice", secret, "49160-49200")
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	sessions := NewSessions(time.Hour, 10, nil, []string{"turn:127.0.0.1:34780?transport=udp"}, secret)
	sess, _ := sessions.Create()
	cred := sess.ICE.TURN[0]

	conn, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	client, err := turn.NewClient(&turn.ClientConfig{
		STUNServerAddr: "127.0.0.1:34780",
		TURNServerAddr: "127.0.0.1:34780",
		Username:       cred.Username,
		Password:       cred.Credential,
		Realm:          "edifice",
		Conn:           conn,
	})
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	if err := client.Listen(); err != nil {
		t.Fatal(err)
	}
	relay, err := client.Allocate()
	if err != nil {
		t.Fatalf("allocation with session credentials failed: %v", err)
	}
	defer relay.Close()
	t.Logf("relayed address: %s", relay.LocalAddr())
	if port := relay.LocalAddr().(*net.UDPAddr).Port; port < 49160 || port > 49200 {
		t.Fatalf("relay port %d outside the configured range", port)
	}

	// wrong password is refused
	bad, _ := turn.NewClient(&turn.ClientConfig{
		STUNServerAddr: "127.0.0.1:34780", TURNServerAddr: "127.0.0.1:34780",
		Username: cred.Username, Password: "wrong", Realm: "edifice", Conn: mustListen(t),
	})
	defer bad.Close()
	bad.Listen()
	if r, err := bad.Allocate(); err == nil {
		r.Close()
		t.Fatal("allocation with a wrong password succeeded")
	}
}

func mustListen(t *testing.T) net.PacketConn {
	c, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close() })
	return c
}
