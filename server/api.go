package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
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
//	                                (both give a game's "lan" addresses only to a
//	                                player from the host's own public address)
//	GET    /v1/signal            -> the signaling relay over WebSocket (websocket.go)
//	GET    /healthz
type API struct {
	registry   *Registry
	sessions   *Sessions
	signaling  *Signaling // nil: no WebSocket relay
	trustProxy bool
	// trustedProxies: with trustProxy, the proxies whose X-Forwarded-For entries
	// are skipped (from the right). Empty: exactly one proxy, the peer.
	trustedProxies []*net.IPNet
	allowPrivate   bool   // games announced from private addresses are listed (LAN, tests)
	publicURL      string // https://host the players use, "" to take it from each request
	limiter        *rateLimiter
	refresh        time.Duration

	hintMu   sync.Mutex
	hintLast time.Time
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

// ParseProxies reads a comma-separated list of IPs and CIDRs.
func ParseProxies(list string) ([]*net.IPNet, error) {
	var out []*net.IPNet
	for _, item := range strings.Split(list, ",") {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		if !strings.Contains(item, "/") {
			ip := net.ParseIP(item)
			if ip == nil {
				return nil, fmt.Errorf("not an IP or CIDR: %q", item)
			}
			bits := 128
			if ip.To4() != nil {
				ip, bits = ip.To4(), 32
			}
			out = append(out, &net.IPNet{IP: ip, Mask: net.CIDRMask(bits, bits)})
			continue
		}
		_, n, err := net.ParseCIDR(item)
		if err != nil {
			return nil, fmt.Errorf("not an IP or CIDR: %q", item)
		}
		out = append(out, n)
	}
	return out, nil
}

func (a *API) trusted(ip net.IP) bool {
	for _, n := range a.trustedProxies {
		if n.Contains(ip) {
			return true
		}
	}
	return false
}

// parseHop reads an X-Forwarded-For entry: an IP, maybe with a port.
func parseHop(s string) net.IP {
	s = strings.TrimSpace(s)
	if ip := net.ParseIP(s); ip != nil {
		return ip
	}
	if host, _, err := net.SplitHostPort(s); err == nil {
		return net.ParseIP(host)
	}
	return nil
}

// clientIP is the address the request came from. Behind reverse proxies
// (-trust-proxy) it is the right-most X-Forwarded-For entry that isn't one of
// our proxies: proxies append the address they got the request from, so
// everything left of that is whatever the client chose to send. Without a list
// of trusted proxies there is exactly one, the peer: the last entry.
func (a *API) clientIP(r *http.Request) string {
	peer, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		peer = r.RemoteAddr
	}
	if !a.trustProxy {
		return peer
	}
	if len(a.trustedProxies) > 0 {
		if ip := net.ParseIP(peer); ip == nil || !a.trusted(ip) {
			return peer // not from one of our proxies: its header means nothing
		}
	}
	var hops []string
	for _, h := range r.Header.Values("X-Forwarded-For") {
		hops = append(hops, strings.Split(h, ",")...)
	}
	found := peer
	for i := len(hops) - 1; i >= 0; i-- {
		ip := parseHop(hops[i])
		if ip == nil {
			return found // garbage: the last address a proxy of ours vouched for
		}
		found = ip.String()
		if i == 0 || !a.trusted(ip) {
			return found
		}
	}
	return found
}

var cgnat = &net.IPNet{IP: net.IPv4(100, 64, 0, 0).To4(), Mask: net.CIDRMask(10, 32)}

// privateAddress: an address players on the Internet can't reach (private,
// loopback, link-local, carrier-grade NAT, unspecified).
func privateAddress(s string) bool {
	ip := net.ParseIP(s)
	if ip == nil {
		return true
	}
	return ip.IsPrivate() || ip.IsLoopback() || ip.IsLinkLocalUnicast() || ip.IsUnspecified() ||
		ip.IsLinkLocalMulticast() || cgnat.Contains(ip)
}

// proxyHint logs (once a minute at most) that a game from a private address
// was refused, and what is probably wrong when it comes through a proxy.
func (a *API) proxyHint(r *http.Request, ip string) {
	a.hintMu.Lock()
	defer a.hintMu.Unlock()
	if time.Since(a.hintLast) < time.Minute {
		return
	}
	a.hintLast = time.Now()
	hint := "(-allow-private lists them anyway, for LAN tests)"
	if !a.trustProxy {
		hint = "(behind a reverse proxy? set EDIFICE_TRUST_PROXY=1 so that the lobby reads X-Forwarded-For)"
	} else if r.Header.Get("X-Forwarded-For") == "" {
		hint = "(EDIFICE_TRUST_PROXY=1, but the request has no X-Forwarded-For: does the proxy set it?)"
	} else if len(a.trustedProxies) == 0 {
		hint = "(more than one reverse proxy in front? list them in EDIFICE_TRUSTED_PROXIES)"
	}
	log.Printf("refused a game announced from the private address %s %s", ip, hint)
}

// signalingURL is the WebSocket relay's URL for the player making r.
func (a *API) signalingURL(r *http.Request) string {
	if a.signaling == nil {
		return ""
	}
	scheme, host := "ws", r.Host
	if a.publicURL != "" {
		u, err := url.Parse(a.publicURL)
		if err != nil || u.Host == "" {
			return ""
		}
		host = u.Host
		if u.Scheme == "https" {
			scheme = "wss"
		}
	} else {
		proto := ""
		if a.trustProxy {
			fwd := strings.Split(r.Header.Get("X-Forwarded-Proto"), ",")
			proto = strings.TrimSpace(fwd[len(fwd)-1])
		}
		if r.TLS != nil || strings.EqualFold(proto, "https") {
			scheme = "wss"
		}
	}
	if host == "" || !printable(host) || strings.ContainsAny(host, "/@?#,") {
		return ""
	}
	return scheme + "://" + host + "/v1/signal"
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
	lookup := r.Method == http.MethodGet && (strings.HasPrefix(path, "/v1/servers/") || path == "/v1/signal")
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
		out := *sess
		out.SignalingURL = a.signalingURL(r)
		writeJSON(w, http.StatusCreated, out)

	case path == "/v1/signal" && a.signaling != nil:
		a.signaling.ServeWebSocket(w, r)

	case path == "/v1/servers" && r.Method == http.MethodGet:
		protocol, _ := strconv.Atoi(r.URL.Query().Get("protocol"))
		writeJSON(w, http.StatusOK, map[string]interface{}{"servers": a.registry.List(protocol, a.clientIP(r))})

	case path == "/v1/servers" && r.Method == http.MethodPost:
		ip := a.clientIP(r)
		if !a.allowPrivate && privateAddress(ip) {
			a.proxyHint(r, ip)
			writeError(w, http.StatusBadRequest, "the lobby sees this game come from a private address ("+ip+
				"): players on the Internet could not reach it")
			return
		}
		ann, ok := readAnnouncement(w, r)
		if !ok {
			return
		}
		a.vouch(&ann)
		id, token, err := a.registry.Register(ip, ann)
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
			s, ok := a.registry.Get(id, a.clientIP(r))
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
