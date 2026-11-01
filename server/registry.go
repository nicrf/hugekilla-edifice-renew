package main

import (
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"errors"
	"sort"
	"strings"
	"sync"
	"time"
)

// Server is a game announced by its host. Address is the IP the lobby saw the
// announcement come from, never one the host claims.
type Server struct {
	ID         string `json:"id"`
	Name       string `json:"name"`
	Address    string `json:"address"`
	Port       int    `json:"port"`
	Version    string `json:"version"`
	Protocol   int    `json:"protocol"`
	Players    int    `json:"players"`
	MaxPlayers int    `json:"maxplayers"`
	Mode       int    `json:"mode"`
	Visibility string `json:"visibility"`
	Identity   string `json:"identity,omitempty"` // GameNetworkingSockets identity, for P2P

	token    string
	lastSeen time.Time
}

// Announcement is what a host sends when it registers or refreshes its game.
type Announcement struct {
	Name       string `json:"name"`
	Port       int    `json:"port"`
	Version    string `json:"version"`
	Protocol   int    `json:"protocol"`
	Players    int    `json:"players"`
	MaxPlayers int    `json:"maxplayers"`
	Mode       int    `json:"mode"`
	Visibility string `json:"visibility"` // public, friends, private
	Identity   string `json:"identity"`
}

var (
	ErrTooMany    = errors.New("too many servers")
	ErrNotFound   = errors.New("no such server")
	ErrBadToken   = errors.New("bad token")
	ErrBadRequest = errors.New("invalid announcement")
)

// Registry keeps the announced games; entries expire when their host stops
// refreshing them.
type Registry struct {
	mu       sync.Mutex
	servers  map[string]*Server
	ttl      time.Duration
	maxPerIP int
	maxTotal int
	now      func() time.Time
}

func NewRegistry(ttl time.Duration, maxPerIP, maxTotal int) *Registry {
	return &Registry{
		servers:  map[string]*Server{},
		ttl:      ttl,
		maxPerIP: maxPerIP,
		maxTotal: maxTotal,
		now:      time.Now,
	}
}

func randomHex(n int) string {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

// cleanText keeps printable ASCII (the game font) and cuts to max characters.
func cleanText(s string, max int) string {
	var b strings.Builder
	for _, r := range s {
		if r >= 32 && r < 127 && b.Len() < max {
			b.WriteRune(r)
		}
	}
	return strings.TrimSpace(b.String())
}

// codeAlphabet: the characters of game codes, without the look-alikes 0/O and 1/I.
const codeAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"

// newCode is a game id short enough to be read out or typed: 6 characters,
// 30 random bits.
func newCode() string {
	b := make([]byte, 6)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	for i := range b {
		b[i] = codeAlphabet[int(b[i])%len(codeAlphabet)]
	}
	return string(b)
}

// NormalizeCode turns what a player typed ("abc-123 ") into an id.
func NormalizeCode(s string) string {
	var b strings.Builder
	for _, r := range strings.ToUpper(s) {
		if r != '-' && r != ' ' {
			b.WriteRune(r)
		}
	}
	return b.String()
}

func (a *Announcement) validate() error {
	a.Name = cleanText(a.Name, 64)
	a.Version = cleanText(a.Version, 15)
	a.Identity = cleanText(a.Identity, 64)
	if strings.ContainsRune(a.Identity, ' ') {
		return ErrBadRequest
	}
	switch a.Visibility {
	case "":
		a.Visibility = "public"
	case "public", "friends", "private":
	default:
		return ErrBadRequest
	}
	if a.Name == "" || a.Port < 1 || a.Port > 65535 || a.Protocol < 0 || a.Protocol > 65535 ||
		a.Players < 0 || a.Players > 255 || a.MaxPlayers < 1 || a.MaxPlayers > 255 || a.Mode < 0 || a.Mode > 255 {
		return ErrBadRequest
	}
	return nil
}

func (s *Server) apply(a Announcement) {
	s.Name, s.Port, s.Version, s.Protocol = a.Name, a.Port, a.Version, a.Protocol
	s.Players, s.MaxPlayers, s.Mode, s.Visibility, s.Identity = a.Players, a.MaxPlayers, a.Mode, a.Visibility, a.Identity
}

// Register adds a game seen from ip and returns its id and the token needed to
// refresh or remove it.
func (r *Registry) Register(ip string, a Announcement) (id, token string, err error) {
	if err := a.validate(); err != nil {
		return "", "", err
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.expireLocked()
	perIP := 0
	for _, s := range r.servers {
		if s.Address == ip {
			perIP++
		}
	}
	if perIP >= r.maxPerIP || len(r.servers) >= r.maxTotal {
		return "", "", ErrTooMany
	}
	id = newCode()
	for r.servers[id] != nil {
		id = newCode()
	}
	s := &Server{ID: id, Address: ip, token: randomHex(16), lastSeen: r.now()}
	s.apply(a)
	r.servers[s.ID] = s
	return s.ID, s.token, nil
}

func (r *Registry) find(id, token string) (*Server, error) {
	s, ok := r.servers[id]
	if !ok {
		return nil, ErrNotFound
	}
	if subtle.ConstantTimeCompare([]byte(s.token), []byte(token)) != 1 {
		return nil, ErrBadToken
	}
	return s, nil
}

// Refresh updates a game and keeps it listed for another ttl.
func (r *Registry) Refresh(id, token string, a Announcement) error {
	if err := a.validate(); err != nil {
		return err
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.expireLocked()
	s, err := r.find(id, token)
	if err != nil {
		return err
	}
	s.apply(a)
	s.lastSeen = r.now()
	return nil
}

func (r *Registry) Remove(id, token string) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, err := r.find(id, token); err != nil {
		return err
	}
	delete(r.servers, id)
	return nil
}

// List returns the public games, most players first; protocol 0 means any.
func (r *Registry) List(protocol int) []Server {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.expireLocked()
	out := []Server{}
	for _, s := range r.servers {
		if s.Visibility != "public" || (protocol != 0 && s.Protocol != protocol) {
			continue
		}
		out = append(out, *s)
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Players != out[j].Players {
			return out[i].Players > out[j].Players
		}
		return out[i].Name < out[j].Name
	})
	return out
}

// Get returns one game by id (its game code, any case), whatever its
// visibility (shared by invitation).
func (r *Registry) Get(id string) (Server, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.expireLocked()
	s, ok := r.servers[NormalizeCode(id)]
	if !ok {
		return Server{}, false
	}
	return *s, true
}

func (r *Registry) expireLocked() {
	now := r.now()
	for id, s := range r.servers {
		if now.Sub(s.lastSeen) > r.ttl {
			delete(r.servers, id)
		}
	}
}
