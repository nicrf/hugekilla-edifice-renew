package main

import (
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

// API is the HTTP JSON interface of the lobby:
//
//	POST   /v1/sessions          -> identity, token, STUN/TURN servers
//	POST   /v1/servers           -> announce a game: id, token, refresh interval
//	                                (its identity listed only with its session token)
//	PUT    /v1/servers/{id}      -> refresh it (Authorization: Bearer token)
//	DELETE /v1/servers/{id}      -> remove it (Authorization: Bearer token)
//	GET    /v1/servers?protocol=N -> public games
//	GET    /v1/servers/{id}      -> one game (private games are shared by id, a
//	                                6-character game code)
//	GET    /healthz
type API struct {
	registry   *Registry
	sessions   *Sessions
	trustProxy bool
	limiter    *rateLimiter
	refresh    time.Duration
}

const maxBody = 4096

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(v)
}

func writeError(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": msg})
}

// clientIP is the address the request came from; behind a reverse proxy
// (-trust-proxy) the first X-Forwarded-For entry.
func (a *API) clientIP(r *http.Request) string {
	if a.trustProxy {
		if fwd := r.Header.Get("X-Forwarded-For"); fwd != "" {
			first := strings.TrimSpace(strings.Split(fwd, ",")[0])
			if ip := net.ParseIP(first); ip != nil {
				return ip.String()
			}
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func bearer(r *http.Request) string {
	return strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
}

func readAnnouncement(w http.ResponseWriter, r *http.Request) (Announcement, bool) {
	var a Announcement
	r.Body = http.MaxBytesReader(w, r.Body, maxBody)
	if err := json.NewDecoder(r.Body).Decode(&a); err != nil {
		writeError(w, http.StatusBadRequest, "invalid json")
		return a, false
	}
	return a, true
}

// vouch keeps the announced identity only if the announcer proves it is theirs
// with its session token: clients connect P2P to the identity the lobby lists,
// so nobody may list a game under someone else's. Without proof the game is
// still listed, without identity (clients then join it by its address).
func (a *API) vouch(ann *Announcement) {
	if ann.Identity != "" && !a.sessions.Check(ann.Identity, ann.Session) {
		ann.Identity = ""
	}
	ann.Session = ""
}

func statusFor(err error) int {
	switch {
	case errors.Is(err, ErrBadRequest):
		return http.StatusBadRequest
	case errors.Is(err, ErrNotFound):
		return http.StatusNotFound
	case errors.Is(err, ErrBadToken):
		return http.StatusForbidden
	case errors.Is(err, ErrTooMany):
		return http.StatusTooManyRequests
	}
	return http.StatusInternalServerError
}

func (a *API) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimSuffix(r.URL.Path, "/")
	if path == "/healthz" {
		w.Write([]byte("ok\n"))
		return
	}
	// writes are rate limited per IP, and so are lookups by game code (they
	// would otherwise let anyone enumerate the private games); the list is cheap
	lookup := r.Method == http.MethodGet && strings.HasPrefix(path, "/v1/servers/")
	if (r.Method != http.MethodGet || lookup) && !a.limiter.allow(a.clientIP(r)) {
		writeError(w, http.StatusTooManyRequests, "slow down")
		return
	}
	switch {
	case path == "/v1/sessions" && r.Method == http.MethodPost:
		sess, err := a.sessions.Create()
		if err != nil {
			writeError(w, statusFor(err), err.Error())
			return
		}
		writeJSON(w, http.StatusCreated, sess)

	case path == "/v1/servers" && r.Method == http.MethodGet:
		protocol, _ := strconv.Atoi(r.URL.Query().Get("protocol"))
		writeJSON(w, http.StatusOK, map[string]interface{}{"servers": a.registry.List(protocol)})

	case path == "/v1/servers" && r.Method == http.MethodPost:
		ann, ok := readAnnouncement(w, r)
		if !ok {
			return
		}
		a.vouch(&ann)
		id, token, err := a.registry.Register(a.clientIP(r), ann)
		if err != nil {
			writeError(w, statusFor(err), err.Error())
			return
		}
		writeJSON(w, http.StatusCreated, map[string]interface{}{
			"id": id, "token": token, "refresh_seconds": int(a.refresh / time.Second),
		})

	case strings.HasPrefix(path, "/v1/servers/"):
		id := strings.TrimPrefix(path, "/v1/servers/")
		switch r.Method {
		case http.MethodGet:
			s, ok := a.registry.Get(id)
			if !ok {
				writeError(w, http.StatusNotFound, ErrNotFound.Error())
				return
			}
			writeJSON(w, http.StatusOK, s)
		case http.MethodPut:
			ann, ok := readAnnouncement(w, r)
			if !ok {
				return
			}
			a.vouch(&ann)
			if err := a.registry.Refresh(id, bearer(r), ann); err != nil {
				writeError(w, statusFor(err), err.Error())
				return
			}
			w.WriteHeader(http.StatusNoContent)
		case http.MethodDelete:
			if err := a.registry.Remove(id, bearer(r)); err != nil {
				writeError(w, statusFor(err), err.Error())
				return
			}
			w.WriteHeader(http.StatusNoContent)
		default:
			writeError(w, http.StatusMethodNotAllowed, "method not allowed")
		}

	default:
		writeError(w, http.StatusNotFound, "not found")
	}
}

// rateLimiter allows burst requests per IP, refilled at rate per second.
type rateLimiter struct {
	mu      sync.Mutex
	buckets map[string]*bucket
	rate    float64
	burst   float64
	now     func() time.Time
}

type bucket struct {
	tokens float64
	last   time.Time
}

func newRateLimiter(perMinute, burst int) *rateLimiter {
	return &rateLimiter{buckets: map[string]*bucket{}, rate: float64(perMinute) / 60, burst: float64(burst), now: time.Now}
}

func (l *rateLimiter) allow(ip string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	b, ok := l.buckets[ip]
	if !ok {
		b = &bucket{tokens: l.burst, last: now}
		l.buckets[ip] = b
	}
	b.tokens += now.Sub(b.last).Seconds() * l.rate
	if b.tokens > l.burst {
		b.tokens = l.burst
	}
	b.last = now
	if len(l.buckets) > 100000 { // forget idle IPs
		for k, v := range l.buckets {
			if now.Sub(v.last) > time.Hour {
				delete(l.buckets, k)
			}
		}
	}
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}
