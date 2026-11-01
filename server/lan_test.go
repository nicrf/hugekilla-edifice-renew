package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"
	"time"
)

func TestCleanLAN(t *testing.T) {
	got := cleanLAN([]string{
		"192.168.1.20", "10.0.0.5", "172.16.3.4", "172.31.255.1", // private: kept
		"192.168.1.20",                                                               // twice
		"172.32.0.1", "8.8.8.8", "127.0.0.1", "169.254.1.1", "100.64.0.1", "0.0.0.0", // not a LAN address
		"::ffff:192.168.1.21", "fd00::1", "192.168.1", "192.168.1.300", "host.lan", "", // not dotted IPv4
	})
	want := []string{"192.168.1.20", "10.0.0.5", "172.16.3.4", "172.31.255.1"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("cleanLAN = %v, want %v", got, want)
	}
	if got := cleanLAN([]string{"10.0.0.1", "10.0.0.2", "10.0.0.3", "10.0.0.4", "10.0.0.5"}); len(got) != MaxLAN {
		t.Fatalf("more than %d kept: %v", MaxLAN, got)
	}
	a := announcement("many")
	a.LAN = make([]string, 100)
	if err := a.validate(); err != ErrBadRequest {
		t.Fatalf("100 LAN entries: %v", err)
	}
}

func TestRegistryLANOnlyForTheSameAddress(t *testing.T) {
	r := NewRegistry(time.Minute, 4, 100)
	a := announcement("Home fort")
	a.LAN = []string{"192.168.1.20", "8.8.8.8"}
	id, token, err := r.Register("203.0.113.7", a)
	if err != nil {
		t.Fatal(err)
	}
	if l := r.List(0, "203.0.113.7"); len(l) != 1 || !reflect.DeepEqual(l[0].LAN, []string{"192.168.1.20"}) {
		t.Fatalf("same address, list: %+v", l)
	}
	if s, _ := r.Get(id, "::ffff:203.0.113.7"); !reflect.DeepEqual(s.LAN, []string{"192.168.1.20"}) {
		t.Fatalf("same address (IPv4-mapped), get: %+v", s)
	}
	for _, viewer := range []string{"198.51.100.1", "", "203.0.113.8", "garbage"} {
		if l := r.List(0, viewer); len(l) != 1 || l[0].LAN != nil {
			t.Fatalf("list for %q: %+v", viewer, l)
		}
		if s, _ := r.Get(id, viewer); s.LAN != nil {
			t.Fatalf("get for %q: %+v", viewer, s)
		}
	}
	// the copy given out isn't the stored one
	l := r.List(0, "203.0.113.7")
	l[0].LAN[0] = "10.9.9.9"
	if s, _ := r.Get(id, "203.0.113.7"); s.LAN[0] != "192.168.1.20" {
		t.Fatalf("stored LAN changed through a listing: %v", s.LAN)
	}
	// a refresh replaces them; without the field (an older game) there are none
	a.LAN = []string{"10.1.2.3"}
	if err := r.Refresh(id, token, a); err != nil {
		t.Fatal(err)
	}
	if s, _ := r.Get(id, "203.0.113.7"); !reflect.DeepEqual(s.LAN, []string{"10.1.2.3"}) {
		t.Fatalf("after refresh: %v", s.LAN)
	}
	if err := r.Refresh(id, token, announcement("Home fort")); err != nil {
		t.Fatal(err)
	}
	if s, _ := r.Get(id, "203.0.113.7"); s.LAN != nil {
		t.Fatalf("after a refresh without lan: %v", s.LAN)
	}
}

// over HTTP behind a reverse proxy: the host and a player of its network come
// from the same public address (X-Forwarded-For), another player doesn't; the
// JSON of the others never has "lan" in it
func TestHTTPLANBehindProxy(t *testing.T) {
	proxies, _ := ParseProxies("10.1.1.1")
	api := &API{registry: NewRegistry(time.Minute, 4, 100), sessions: NewSessions(time.Hour, 10, nil, nil, "x"),
		trustProxy: true, trustedProxies: proxies, limiter: newRateLimiter(600, 100), refresh: 20 * time.Second}
	do := func(method, path, fwd string, body []byte) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, path, bytes.NewReader(body))
		r.RemoteAddr = "10.1.1.1:4000" // the proxy
		r.Header.Set("X-Forwarded-For", fwd)
		w := httptest.NewRecorder()
		api.ServeHTTP(w, r)
		return w
	}
	// the host: raw JSON, as the game writes it
	w := do(http.MethodPost, "/v1/servers", "203.0.113.7", []byte(`{"name":"Home fort","version":"0.4.2",`+
		`"visibility":"private","identity":"","session":"","lan":["192.168.1.20","1.1.1.1"],`+
		`"port":27020,"protocol":5,"players":1,"maxplayers":32,"mode":0}`))
	if w.Code != http.StatusCreated {
		t.Fatalf("announce: %d %s", w.Code, w.Body)
	}
	var created struct{ ID string }
	json.Unmarshal(w.Body.Bytes(), &created)
	pub := announcement("Public fort")
	pub.LAN = []string{"10.0.0.8"}
	body, _ := json.Marshal(pub)
	if w := do(http.MethodPost, "/v1/servers", "203.0.113.7", body); w.Code != http.StatusCreated {
		t.Fatalf("announce: %d", w.Code)
	}

	lan := func(w *httptest.ResponseRecorder) []string {
		var s Server
		json.Unmarshal(w.Body.Bytes(), &s)
		return s.LAN
	}
	if got := lan(do(http.MethodGet, "/v1/servers/"+created.ID, "203.0.113.7", nil)); !reflect.DeepEqual(got, []string{"192.168.1.20"}) {
		t.Fatalf("same network, by code: %v", got)
	}
	w = do(http.MethodGet, "/v1/servers?protocol=0", "203.0.113.7", nil)
	var list struct{ Servers []Server }
	json.Unmarshal(w.Body.Bytes(), &list)
	if len(list.Servers) != 1 || !reflect.DeepEqual(list.Servers[0].LAN, []string{"10.0.0.8"}) {
		t.Fatalf("same network, list: %s", w.Body)
	}

	// another network; one that pretends to be the host's by sending its own
	// X-Forwarded-For (the proxy appends the real address); one that skips the proxy
	for name, w := range map[string]*httptest.ResponseRecorder{
		"other code": do(http.MethodGet, "/v1/servers/"+created.ID, "198.51.100.9", nil),
		"other list": do(http.MethodGet, "/v1/servers?protocol=0", "198.51.100.9", nil),
		"spoof code": do(http.MethodGet, "/v1/servers/"+created.ID, "203.0.113.7, 198.51.100.9", nil),
		"spoof list": do(http.MethodGet, "/v1/servers?protocol=0", "203.0.113.7, 198.51.100.9", nil),
		"direct code": func() *httptest.ResponseRecorder {
			r := httptest.NewRequest(http.MethodGet, "/v1/servers/"+created.ID, nil)
			r.RemoteAddr = "198.51.100.9:4000"
			r.Header.Set("X-Forwarded-For", "203.0.113.7")
			w := httptest.NewRecorder()
			api.ServeHTTP(w, r)
			return w
		}(),
	} {
		if w.Code != http.StatusOK || strings.Contains(w.Body.String(), "lan") || strings.Contains(w.Body.String(), "192.168") ||
			strings.Contains(w.Body.String(), "10.0.0.8") {
			t.Errorf("%s: %d %s", name, w.Code, w.Body)
		}
	}
}
