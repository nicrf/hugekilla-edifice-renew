package main

import (
	"crypto/hmac"
	"crypto/sha1"
	"crypto/subtle"
	"encoding/base64"
	"fmt"
	"sync"
	"time"
)

// Session gives a player a network identity (used by GameNetworkingSockets and
// the signaling relay) and short-lived TURN credentials. Identities are chosen
// by the lobby so nobody can pick someone else's.
type Session struct {
	Identity string    `json:"identity"`
	Token    string    `json:"token"`
	Expires  time.Time `json:"expires"`
	ICE      ICEConfig `json:"ice"`
	// Signaling is the host:port of the signaling relay, empty when the lobby
	// doesn't know its public name (the game then uses the lobby's host, port 10000)
	Signaling string `json:"signaling,omitempty"`
}

type ICEConfig struct {
	STUN []string     `json:"stun"`
	TURN []TURNServer `json:"turn"`
}

type TURNServer struct {
	URL        string `json:"url"`
	Username   string `json:"username"`
	Credential string `json:"credential"`
}

type Sessions struct {
	mu         sync.Mutex
	sessions   map[string]*Session // by identity
	lifetime   time.Duration
	maxTotal   int
	stunURLs   []string
	turnURLs   []string // empty: TURN disabled
	turnSecret string
	signaling  string // host:port handed out with each session, may be empty
	now        func() time.Time
}

func NewSessions(lifetime time.Duration, maxTotal int, stunURLs, turnURLs []string, turnSecret string) *Sessions {
	return &Sessions{
		sessions:   map[string]*Session{},
		lifetime:   lifetime,
		maxTotal:   maxTotal,
		stunURLs:   stunURLs,
		turnURLs:   turnURLs,
		turnSecret: turnSecret,
		now:        time.Now,
	}
}

// turnCredentials follows the TURN REST API convention (also used by coturn):
// username "expiry:identity", password base64(HMAC-SHA1(secret, username)).
func turnCredentials(secret, identity string, expires time.Time) (username, password string) {
	username = fmt.Sprintf("%d:%s", expires.Unix(), identity)
	mac := hmac.New(sha1.New, []byte(secret))
	mac.Write([]byte(username))
	return username, base64.StdEncoding.EncodeToString(mac.Sum(nil))
}

func (s *Sessions) Create() (*Session, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.expireLocked()
	if len(s.sessions) >= s.maxTotal {
		return nil, ErrTooMany
	}
	expires := s.now().Add(s.lifetime)
	sess := &Session{
		Identity:  "str:edifice-" + randomHex(8),
		Token:     randomHex(16),
		Expires:   expires,
		ICE:       ICEConfig{STUN: s.stunURLs, TURN: []TURNServer{}},
		Signaling: s.signaling,
	}
	if len(s.turnURLs) > 0 {
		user, pass := turnCredentials(s.turnSecret, sess.Identity, expires)
		for _, u := range s.turnURLs {
			sess.ICE.TURN = append(sess.ICE.TURN, TURNServer{URL: u, Username: user, Credential: pass})
		}
	}
	s.sessions[sess.Identity] = sess
	return sess, nil
}

// Check tells whether token belongs to identity's live session.
func (s *Sessions) Check(identity, token string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.sessions[identity]
	if !ok || s.now().After(sess.Expires) {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(sess.Token), []byte(token)) == 1
}

func (s *Sessions) expireLocked() {
	now := s.now()
	for id, sess := range s.sessions {
		if now.After(sess.Expires) {
			delete(s.sessions, id)
		}
	}
}

func (s *Sessions) Expire() {
	s.mu.Lock()
	s.expireLocked()
	s.mu.Unlock()
}
