package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestParseCORSOrigins(t *testing.T) {
	c, err := ParseCORSOrigins(" https://nicrf.github.io, HTTP://Play.Example.org:8443/ ")
	if err != nil {
		t.Fatal(err)
	}
	for origin, want := range map[string]bool{
		"https://nicrf.github.io":       true,
		"http://play.example.org:8443":  true,
		"http://nicrf.github.io":        false, // another scheme is another origin
		"https://nicrf.github.io.evil":  false,
		"https://evil.nicrf.github.io":  false,
		"null":                          false, // file:// pages
		"":                              false,
		"https://play.example.org:8443": false,
	} {
		if got := c.allowed(origin); got != want {
			t.Errorf("allowed(%q) = %v, want %v", origin, got, want)
		}
	}
	for _, list := range []string{"", "none"} {
		c, err := ParseCORSOrigins(list)
		if err != nil || c.allowed("https://nicrf.github.io") {
			t.Errorf("%q: %v, allows an origin", list, err)
		}
	}
	if c, err := ParseCORSOrigins("*"); err != nil || !c.allowed("https://anything.example") {
		t.Errorf("*: %v", err)
	}
	for _, bad := range []string{"nicrf.github.io", "https://nicrf.github.io/hugekilla-edifice-renew/",
		"ftp://x.org", "https://", "https://x.org?a=1", "https://user@x.org"} {
		if _, err := ParseCORSOrigins(bad); err == nil {
			t.Errorf("%q accepted", bad)
		}
	}
}

func corsAPI(t *testing.T, origins string, perMinute int) *API {
	cors, err := ParseCORSOrigins(origins)
	if err != nil {
		t.Fatal(err)
	}
	return &API{registry: NewRegistry(time.Minute, 4, 100), sessions: NewSessions(time.Hour, 10, nil, nil, "x"),
		allowPrivate: true, limiter: newRateLimiter(perMinute, (perMinute+2)/3), refresh: 20 * time.Second, cors: cors}
}

func TestCORSRequests(t *testing.T) {
	api := corsAPI(t, DefaultCORSOrigins, 600)
	page := "https://nicrf.github.io"

	// the game list from the page: allowed, and the answer says so
	req := httptest.NewRequest("GET", "/v1/servers?protocol=5", nil)
	req.Header.Set("Origin", page)
	rec := httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK || rec.Header().Get("Access-Control-Allow-Origin") != page ||
		!strings.Contains(rec.Header().Get("Vary"), "Origin") || !strings.Contains(rec.Body.String(), "servers") {
		t.Fatalf("GET from the page: %d %v %s", rec.Code, rec.Header(), rec.Body)
	}

	// from another page, or the native game (no Origin): served, without CORS headers
	for _, origin := range []string{"https://evil.example", ""} {
		req := httptest.NewRequest("GET", "/v1/servers", nil)
		if origin != "" {
			req.Header.Set("Origin", origin)
		}
		rec := httptest.NewRecorder()
		api.ServeHTTP(rec, req)
		if rec.Code != http.StatusOK || rec.Header().Get("Access-Control-Allow-Origin") != "" {
			t.Fatalf("GET from %q: %d %v", origin, rec.Code, rec.Header())
		}
	}

	// a preflight before PUT with the token: methods and headers the game uses
	req = httptest.NewRequest("OPTIONS", "/v1/servers/ABC123", nil)
	req.Header.Set("Origin", page)
	req.Header.Set("Access-Control-Request-Method", "PUT")
	req.Header.Set("Access-Control-Request-Headers", "authorization, content-type")
	rec = httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	h := rec.Header()
	if rec.Code != http.StatusNoContent || h.Get("Access-Control-Allow-Origin") != page ||
		!strings.Contains(h.Get("Access-Control-Allow-Methods"), "PUT") ||
		!strings.Contains(h.Get("Access-Control-Allow-Methods"), "DELETE") ||
		!strings.Contains(strings.ToLower(h.Get("Access-Control-Allow-Headers")), "authorization") ||
		!strings.Contains(strings.ToLower(h.Get("Access-Control-Allow-Headers")), "content-type") {
		t.Fatalf("preflight: %d %v", rec.Code, h)
	}

	// a preflight from another origin: no permission given
	req = httptest.NewRequest("OPTIONS", "/v1/sessions", nil)
	req.Header.Set("Origin", "https://evil.example")
	req.Header.Set("Access-Control-Request-Method", "POST")
	rec = httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Header().Get("Access-Control-Allow-Origin") != "" || rec.Header().Get("Access-Control-Allow-Methods") != "" {
		t.Fatalf("preflight from another origin: %v", rec.Header())
	}

	// a session for the page (POST, rate limited as before), with the header
	req = httptest.NewRequest("POST", "/v1/sessions", nil)
	req.Header.Set("Origin", page)
	rec = httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Code != http.StatusCreated || rec.Header().Get("Access-Control-Allow-Origin") != page {
		t.Fatalf("POST /v1/sessions from the page: %d %v %s", rec.Code, rec.Header(), rec.Body)
	}
}

// preflights don't use the write budget: a page's PUT needs one before each
func TestCORSPreflightNotRateLimited(t *testing.T) {
	api := corsAPI(t, DefaultCORSOrigins, 3)
	for i := 0; i < 20; i++ {
		req := httptest.NewRequest("OPTIONS", "/v1/servers", nil)
		req.Header.Set("Origin", "https://nicrf.github.io")
		req.Header.Set("Access-Control-Request-Method", "POST")
		rec := httptest.NewRecorder()
		api.ServeHTTP(rec, req)
		if rec.Code != http.StatusNoContent {
			t.Fatalf("preflight %d: %d", i, rec.Code)
		}
	}
	// the real write still gets through
	req := httptest.NewRequest("POST", "/v1/sessions", nil)
	req.Header.Set("Origin", "https://nicrf.github.io")
	rec := httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Code != http.StatusCreated {
		t.Fatalf("POST after the preflights: %d %s", rec.Code, rec.Body)
	}
}

// a lobby built without any CORS setting (an API made by older code or a test)
// answers as before
func TestCORSNil(t *testing.T) {
	api := corsAPI(t, "", 600)
	api.cors = nil
	req := httptest.NewRequest("GET", "/v1/servers", nil)
	req.Header.Set("Origin", "https://nicrf.github.io")
	rec := httptest.NewRecorder()
	api.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK || rec.Header().Get("Access-Control-Allow-Origin") != "" {
		t.Fatalf("%d %v", rec.Code, rec.Header())
	}
}
