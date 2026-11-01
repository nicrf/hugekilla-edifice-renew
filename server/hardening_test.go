package main

import (
	"bufio"
	"bytes"
	"crypto/tls"
	"encoding/binary"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestClientIPForwardedFor(t *testing.T) {
	proxies, err := ParseProxies("10.0.0.0/8, 192.0.2.7")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := ParseProxies("10.0.0.0/8,nope"); err == nil {
		t.Fatal("a bad proxy list is accepted")
	}
	cases := []struct {
		name    string
		api     *API
		peer    string
		headers []string
		want    string
	}{
		{"no proxy: the header is ignored", &API{}, "203.0.113.1:5000", []string{"198.51.100.9"}, "203.0.113.1"},
		{"one proxy, no header", &API{trustProxy: true}, "172.18.0.2:5000", nil, "172.18.0.2"},
		{"one proxy", &API{trustProxy: true}, "172.18.0.2:5000", []string{"198.51.100.9"}, "198.51.100.9"},
		// the client sent its own X-Forwarded-For, the proxy appended the real address
		{"spoofed first entry", &API{trustProxy: true}, "172.18.0.2:5000", []string{"6.6.6.6, 198.51.100.9"}, "198.51.100.9"},
		{"spoofed header line", &API{trustProxy: true}, "172.18.0.2:5000", []string{"6.6.6.6", "198.51.100.9"}, "198.51.100.9"},
		{"entry with a port", &API{trustProxy: true}, "172.18.0.2:5000", []string{"198.51.100.9:4711"}, "198.51.100.9"},
		{"IPv6", &API{trustProxy: true}, "[fd00::2]:5000", []string{"2001:db8::1"}, "2001:db8::1"},
		{"garbage last entry", &API{trustProxy: true}, "172.18.0.2:5000", []string{"198.51.100.9, nonsense"}, "172.18.0.2"},
		// two proxies of ours: skip them from the right
		{"trusted chain", &API{trustProxy: true, trustedProxies: proxies}, "10.1.1.1:5000",
			[]string{"6.6.6.6, 198.51.100.9, 192.0.2.7, 10.2.2.2"}, "198.51.100.9"},
		{"trusted chain, spoofed trusted entry", &API{trustProxy: true, trustedProxies: proxies}, "10.1.1.1:5000",
			[]string{"10.9.9.9, 198.51.100.9, 10.2.2.2"}, "198.51.100.9"},
		{"trusted chain, all ours", &API{trustProxy: true, trustedProxies: proxies}, "10.1.1.1:5000",
			[]string{"10.3.3.3, 10.2.2.2"}, "10.3.3.3"},
		{"trusted chain, garbage behind ours", &API{trustProxy: true, trustedProxies: proxies}, "10.1.1.1:5000",
			[]string{"nonsense, 10.2.2.2"}, "10.2.2.2"},
		{"peer is not one of our proxies", &API{trustProxy: true, trustedProxies: proxies}, "203.0.113.1:5000",
			[]string{"198.51.100.9"}, "203.0.113.1"},
	}
	for _, c := range cases {
		r := httptest.NewRequest(http.MethodGet, "/", nil)
		r.RemoteAddr = c.peer
		for _, h := range c.headers {
			r.Header.Add("X-Forwarded-For", h)
		}
		if got := c.api.clientIP(r); got != c.want {
			t.Errorf("%s: %q, want %q", c.name, got, c.want)
		}
	}
}

func TestPrivateAddresses(t *testing.T) {
	for _, ip := range []string{"10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1", "127.0.0.1", "169.254.1.1",
		"100.64.0.1", "100.127.255.255", "::1", "fc00::1", "fd12::1", "fe80::1", "0.0.0.0", "::", "::ffff:192.168.1.1", "junk"} {
		if !privateAddress(ip) {
			t.Errorf("%s is not private", ip)
		}
	}
	for _, ip := range []string{"8.8.8.8", "172.32.0.1", "100.128.0.1", "100.63.255.255", "51.222.86.26", "2001:db8::1", "2a01:4f8::1"} {
		if privateAddress(ip) {
			t.Errorf("%s is private", ip)
		}
	}
}

func TestPrivateAnnouncementRefused(t *testing.T) {
	api := &API{registry: NewRegistry(time.Minute, 4, 100), sessions: NewSessions(time.Hour, 10, nil, nil, "x"),
		limiter: newRateLimiter(600, 100), refresh: 20 * time.Second}
	announce := func(fwd string) (int, string) {
		body, _ := json.Marshal(announcement("LAN fort"))
		r := httptest.NewRequest(http.MethodPost, "/v1/servers", bytes.NewReader(body))
		r.RemoteAddr = "127.0.0.1:4000"
		if fwd != "" {
			r.Header.Set("X-Forwarded-For", fwd)
		}
		w := httptest.NewRecorder()
		api.ServeHTTP(w, r)
		var e struct{ Error string }
		json.Unmarshal(w.Body.Bytes(), &e)
		return w.Code, e.Error
	}
	if code, msg := announce(""); code != http.StatusBadRequest || !strings.Contains(msg, "private address (127.0.0.1)") {
		t.Fatalf("from loopback: %d %q", code, msg)
	}
	api.trustProxy = true
	if code, _ := announce("192.168.1.20"); code != http.StatusBadRequest {
		t.Fatalf("from a LAN address through the proxy: %d", code)
	}
	if code, _ := announce("198.51.100.9"); code != http.StatusCreated {
		t.Fatalf("from a public address through the proxy: %d", code)
	}
	if list := api.registry.List(0); len(list) != 1 || list[0].Address != "198.51.100.9" {
		t.Fatalf("list = %+v", list)
	}
	api.trustProxy = false
	api.allowPrivate = true
	if code, _ := announce(""); code != http.StatusCreated {
		t.Fatalf("from loopback with -allow-private: %d", code)
	}
}

func TestSignalingURL(t *testing.T) {
	api := &API{signaling: NewSignaling(nil)}
	req := func(host string, tlsOn bool, proto string) *http.Request {
		r := httptest.NewRequest(http.MethodPost, "/v1/sessions", nil)
		r.Host = host
		if tlsOn {
			r.TLS = &tls.ConnectionState{}
		}
		if proto != "" {
			r.Header.Set("X-Forwarded-Proto", proto)
		}
		return r
	}
	if got := api.signalingURL(req("127.0.0.1:18080", false, "")); got != "ws://127.0.0.1:18080/v1/signal" {
		t.Errorf("plain HTTP: %q", got)
	}
	if got := api.signalingURL(req("lobby.example.org", true, "")); got != "wss://lobby.example.org/v1/signal" {
		t.Errorf("HTTPS: %q", got)
	}
	if got := api.signalingURL(req("lobby.example.org", false, "https")); got != "ws://lobby.example.org/v1/signal" {
		t.Errorf("X-Forwarded-Proto without -trust-proxy: %q", got)
	}
	api.trustProxy = true
	if got := api.signalingURL(req("lobby.example.org", false, "https")); got != "wss://lobby.example.org/v1/signal" {
		t.Errorf("behind an HTTPS proxy: %q", got)
	}
	if got := api.signalingURL(req("evil.org/x?", false, "https")); got != "" {
		t.Errorf("odd Host: %q", got)
	}
	api.publicURL = "https://edifice.example.org"
	if got := api.signalingURL(req("10.0.0.1:8080", false, "")); got != "wss://edifice.example.org/v1/signal" {
		t.Errorf("-public-url: %q", got)
	}
	api.signaling = nil
	if got := api.signalingURL(req("lobby.example.org", true, "")); got != "" {
		t.Errorf("no relay: %q", got)
	}
}

// a minimal WebSocket client, masking its frames as clients must
type wsTestClient struct {
	conn net.Conn
	r    *bufio.Reader
}

func wsMasked(opcode byte, payload []byte, fin bool) []byte {
	b := []byte{opcode}
	if fin {
		b[0] |= 0x80
	}
	switch n := len(payload); {
	case n < 126:
		b = append(b, 0x80|byte(n))
	case n <= 0xFFFF:
		b = append(b, 0x80|126, byte(n>>8), byte(n))
	default:
		b = append(b, 0x80|127)
		b = binary.BigEndian.AppendUint64(b, uint64(n))
	}
	mask := []byte{0x12, 0x34, 0x56, 0x78}
	b = append(b, mask...)
	for i, c := range payload {
		b = append(b, c^mask[i&3])
	}
	return b
}

func dialWS(t *testing.T, addr string) *wsTestClient {
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatal(err)
	}
	conn.SetDeadline(time.Now().Add(3 * time.Second))
	conn.Write([]byte("GET /v1/signal HTTP/1.1\r\nHost: " + addr + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"))
	r := bufio.NewReader(conn)
	resp, err := http.ReadResponse(r, nil)
	if err != nil {
		t.Fatal(err)
	}
	// the example of RFC 6455 1.3
	if resp.StatusCode != http.StatusSwitchingProtocols || resp.Header.Get("Sec-WebSocket-Accept") != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" {
		t.Fatalf("upgrade: %v %v", resp.Status, resp.Header)
	}
	return &wsTestClient{conn, r}
}

func (c *wsTestClient) send(line string) { c.conn.Write(wsMasked(wsText, []byte(line), true)) }

func (c *wsTestClient) recv() (byte, string, error) {
	var h [2]byte
	if _, err := io.ReadFull(c.r, h[:]); err != nil {
		return 0, "", err
	}
	if h[1]&0x80 != 0 {
		return 0, "", errWSProtocol // servers don't mask
	}
	n := int(h[1] & 0x7F)
	if n == 126 {
		var b [2]byte
		io.ReadFull(c.r, b[:])
		n = int(binary.BigEndian.Uint16(b[:]))
	}
	payload := make([]byte, n)
	_, err := io.ReadFull(c.r, payload)
	return h[0] & 0x0F, string(payload), err
}

func TestWebSocketSignaling(t *testing.T) {
	sessions := NewSessions(time.Hour, 10, nil, nil, "x")
	alice, _ := sessions.Create()
	bob, _ := sessions.Create()
	carol, _ := sessions.Create()
	signaling := NewSignaling(sessions)
	api := &API{registry: NewRegistry(time.Minute, 4, 100), sessions: sessions, signaling: signaling,
		limiter: newRateLimiter(600, 100), refresh: 20 * time.Second}
	ts := httptest.NewServer(api)
	defer ts.Close()
	addr := strings.TrimPrefix(ts.URL, "http://")

	// not an upgrade
	if resp, err := http.Get(ts.URL + "/v1/signal"); err != nil || resp.StatusCode != http.StatusBadRequest {
		t.Fatalf("plain GET: %v %v", err, resp.Status)
	}

	// the session tells where the relay is
	resp, _ := http.Post(ts.URL+"/v1/sessions", "application/json", nil)
	var sess Session
	json.NewDecoder(resp.Body).Decode(&sess)
	if sess.SignalingURL != "ws://"+addr+"/v1/signal" {
		t.Fatalf("signaling_url = %q", sess.SignalingURL)
	}

	// over WebSocket, and between WebSocket and TCP
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	go signaling.Serve(l)
	ca := dialWS(t, addr)
	defer ca.conn.Close()
	ca.send(alice.Identity + " " + alice.Token)
	cb := dialWS(t, addr)
	defer cb.conn.Close()
	cb.send(bob.Identity + " " + bob.Token + "\n") // a line end is allowed
	tc, err := net.Dial("tcp", l.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer tc.Close()
	tc.SetDeadline(time.Now().Add(3 * time.Second))
	tc.Write([]byte(carol.Identity + " " + carol.Token + "\n"))
	rc := bufio.NewReader(tc)
	time.Sleep(100 * time.Millisecond) // let them register

	ca.send(bob.Identity + " aGVsbG8=")
	if op, line, err := cb.recv(); err != nil || op != wsText || line != alice.Identity+" aGVsbG8=" {
		t.Fatalf("bob got %d %q %v", op, line, err)
	}
	ca.send(carol.Identity + " dG8gdGNw")
	if line, err := rc.ReadString('\n'); err != nil || line != alice.Identity+" dG8gdGNw\n" {
		t.Fatalf("carol (TCP) got %q %v", line, err)
	}
	tc.Write([]byte(alice.Identity + " ZnJvbSB0Y3A=\n"))
	if _, line, err := ca.recv(); err != nil || line != carol.Identity+" ZnJvbSB0Y3A=" {
		t.Fatalf("alice got %q %v", line, err)
	}

	// pings are answered
	ca.conn.Write(wsMasked(wsPing, []byte("hi"), true))
	if op, line, err := ca.recv(); err != nil || op != wsPong || line != "hi" {
		t.Fatalf("pong: %d %q %v", op, line, err)
	}

	// an impostor is turned away
	cx := dialWS(t, addr)
	defer cx.conn.Close()
	cx.send(alice.Identity + " " + bob.Token)
	if _, line, _ := cx.recv(); line != "ERROR unauthorized" {
		t.Fatalf("impostor got %q", line)
	}

	// unmasked, overlong, and fragmented frames end the connection
	for name, frame := range map[string][]byte{
		"unmasked":   {0x81, 0x02, 'h', 'i'},
		"overlong":   wsMasked(wsText, bytes.Repeat([]byte("a"), 5000), true),
		"fragmented": wsMasked(wsText, []byte("x"), false),
	} {
		c := dialWS(t, addr)
		c.send(carol.Identity + " " + carol.Token)
		c.conn.Write(frame)
		for {
			op, _, err := c.recv()
			if err != nil {
				break // closed
			}
			if op != wsClose {
				t.Fatalf("%s: got opcode %d", name, op)
			}
		}
		c.conn.Close()
	}
}

func FuzzReadFrame(f *testing.F) {
	f.Add(wsMasked(wsText, []byte("str:edifice-1 abcd"), true))
	f.Add(wsMasked(wsPing, nil, true))
	f.Add(wsMasked(wsBinary, bytes.Repeat([]byte("a"), 300), true))
	f.Add([]byte{0x81, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3, 4})
	f.Add([]byte{0x89, 0xFE, 0x01, 0x00})
	f.Fuzz(func(t *testing.T, data []byte) {
		r := bytes.NewReader(data)
		for i := 0; i < 64; i++ {
			fin, opcode, payload, err := readFrame(r, 4096)
			if err != nil {
				return
			}
			if len(payload) > 4096 || opcode > 0xF || (opcode&0x8 != 0 && (len(payload) > 125 || !fin)) {
				t.Fatalf("frame accepted: fin %v opcode %d, %d bytes", fin, opcode, len(payload))
			}
		}
	})
}
