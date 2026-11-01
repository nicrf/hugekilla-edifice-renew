package main

import (
	"fmt"
	"net/http"
	"net/url"
	"strings"
)

// DefaultCORSOrigins: the web version of the game on GitHub Pages (roadmap M14)
const DefaultCORSOrigins = "https://nicrf.github.io"

// CORS lets the web version of the game, a page served from another origin, call
// the API from the browser. Only the configured origins get the
// Access-Control-Allow-* headers; the native game sends no Origin and is not
// concerned. The WebSocket relay (GET /v1/signal) isn't subject to CORS.
type CORS struct {
	origins map[string]bool
	any     bool // "*": every origin (tests, a lobby of one's own)
}

// ParseCORSOrigins reads a comma-separated list of origins (scheme://host[:port],
// no path), "*" for any, "none" (or nothing) for none.
func ParseCORSOrigins(list string) (*CORS, error) {
	c := &CORS{origins: map[string]bool{}}
	for _, item := range strings.Split(list, ",") {
		item = strings.TrimSpace(item)
		switch item {
		case "", "none":
			continue
		case "*":
			c.any = true
			continue
		}
		u, err := url.Parse(item)
		if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" ||
			(u.Path != "" && u.Path != "/") || u.RawQuery != "" || u.Fragment != "" || u.User != nil {
			return nil, fmt.Errorf("not an origin (scheme://host[:port]): %q", item)
		}
		// browsers send the origin in lower case, without a trailing slash
		c.origins[strings.ToLower(u.Scheme+"://"+u.Host)] = true
	}
	return c, nil
}

func (c *CORS) allowed(origin string) bool {
	if c == nil || origin == "" {
		return false
	}
	return c.any || c.origins[origin]
}

// handle adds the CORS headers for an allowed origin, and answers a preflight
// (OPTIONS with Access-Control-Request-Method) itself: true when it did, and the
// request is then done. A preflight from another origin gets no CORS headers, so
// the browser doesn't send the request.
func (c *CORS) handle(w http.ResponseWriter, r *http.Request) bool {
	h := w.Header()
	h.Add("Vary", "Origin") // the answer depends on it: caches must not mix them up
	origin := r.Header.Get("Origin")
	ok := c.allowed(origin)
	if ok {
		h.Set("Access-Control-Allow-Origin", origin)
	}
	if r.Method != http.MethodOptions || r.Header.Get("Access-Control-Request-Method") == "" {
		return false
	}
	if ok {
		// what the game sends: JSON bodies, its token as a Bearer for PUT and DELETE
		h.Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE")
		h.Set("Access-Control-Allow-Headers", "Authorization, Content-Type")
		h.Set("Access-Control-Max-Age", "3600")
	}
	w.WriteHeader(http.StatusNoContent)
	return true
}
