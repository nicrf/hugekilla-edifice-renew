package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// "webrtc" (API 4): listed only for a host whose identity is vouched for, since
// browsers reach it through that identity (signaling relay)
func TestWebRTCFlag(t *testing.T) {
	api := &API{registry: NewRegistry(time.Minute, 10, 100), sessions: NewSessions(time.Hour, 10, nil, nil, "x"), allowPrivate: true,
		limiter: newRateLimiter(600, 100), refresh: 20 * time.Second}
	ts := httptest.NewServer(api)
	defer ts.Close()

	resp, _ := http.Post(ts.URL+"/v1/sessions", "application/json", nil)
	var sess Session
	json.NewDecoder(resp.Body).Decode(&sess)
	if sess.Identity == "" {
		t.Fatal("no session")
	}
	announce := func(identity, session string, webrtc bool) (Server, string) {
		a := announcement("Web fort")
		a.Identity, a.Session, a.WebRTC = identity, session, webrtc
		body, _ := json.Marshal(a)
		resp, err := http.Post(ts.URL+"/v1/servers", "application/json", bytes.NewReader(body))
		if err != nil || resp.StatusCode != http.StatusCreated {
			t.Fatalf("announce: %v %v", err, resp.Status)
		}
		var c struct{ ID string }
		json.NewDecoder(resp.Body).Decode(&c)
		resp, _ = http.Get(ts.URL + "/v1/servers/" + c.ID)
		raw, _ := io.ReadAll(resp.Body)
		var s Server
		json.Unmarshal(raw, &s)
		return s, string(raw)
	}
	if s, _ := announce(sess.Identity, sess.Token, true); !s.WebRTC || s.Identity != sess.Identity {
		t.Fatalf("webrtc host not listed as such: %+v", s)
	}
	if s, raw := announce(sess.Identity, sess.Token, false); s.WebRTC || strings.Contains(raw, "webrtc") {
		t.Fatalf("webrtc listed for a host without it: %s", raw)
	}
	if s, _ := announce(sess.Identity, "wrong", true); s.WebRTC || s.Identity != "" {
		t.Fatalf("webrtc kept without a vouched identity: %+v", s)
	}
	if s, _ := announce("", "", true); s.WebRTC {
		t.Fatalf("webrtc kept without an identity: %+v", s)
	}
	resp, _ = http.Get(ts.URL + "/v1/servers?protocol=0")
	var list struct{ Servers []Server }
	json.NewDecoder(resp.Body).Decode(&list)
	n := 0
	for _, s := range list.Servers {
		if s.WebRTC {
			n++
		}
	}
	if len(list.Servers) != 4 || n != 1 {
		t.Fatalf("list: %d games, %d webrtc", len(list.Servers), n)
	}
}
