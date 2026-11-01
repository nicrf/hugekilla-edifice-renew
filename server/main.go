// edifice-lobby: list of public Edifice games, P2P signaling for
// GameNetworkingSockets, and an optional STUN/TURN relay, in one binary.
package main

import (
	"crypto/rand"
	"encoding/hex"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"time"
)

func env(name, fallback string) string {
	if v := os.Getenv(name); v != "" {
		return v
	}
	return fallback
}

func main() {
	httpAddr := flag.String("http", env("EDIFICE_HTTP", ":8080"), "HTTP API address")
	signalAddr := flag.String("signal", env("EDIFICE_SIGNAL", ":10000"), "signaling (TCP) address")
	turnAddr := flag.String("turn", env("EDIFICE_TURN", ":3478"), "STUN/TURN (UDP) address")
	publicHost := flag.String("public-host", env("EDIFICE_PUBLIC_HOST", ""), "host name or IP players use to reach this server (for the STUN/TURN URLs)")
	relayIP := flag.String("turn-relay-ip", env("EDIFICE_TURN_RELAY_IP", ""), "public IPv4 for relayed traffic; empty disables TURN")
	relayPorts := flag.String("turn-relay-ports", env("EDIFICE_TURN_RELAY_PORTS", ""), "UDP port range for relayed traffic, e.g. 49160-49200 (needed behind Docker port mapping)")
	turnSecret := flag.String("turn-secret", env("EDIFICE_TURN_SECRET", ""), "shared secret for TURN credentials (random if empty)")
	trustProxy := flag.Bool("trust-proxy", os.Getenv("EDIFICE_TRUST_PROXY") == "1", "take the client IP from X-Forwarded-For (behind a reverse proxy)")
	ttl := flag.Duration("ttl", 60*time.Second, "a game disappears when its host hasn't refreshed it for this long")
	rateLimit := flag.Int("rate-limit", 30, "write requests and game code lookups allowed per minute and per IP (burst: a third)")
	healthcheck := flag.Bool("healthcheck", false, "check that the HTTP API of a running instance answers, then exit (for Docker)")
	flag.Parse()

	if *healthcheck {
		_, port, _ := net.SplitHostPort(*httpAddr)
		client := http.Client{Timeout: 3 * time.Second}
		resp, err := client.Get("http://127.0.0.1:" + port + "/healthz")
		if err != nil || resp.StatusCode != http.StatusOK {
			os.Exit(1)
		}
		os.Exit(0)
	}

	if *turnSecret == "" {
		b := make([]byte, 32)
		rand.Read(b)
		*turnSecret = hex.EncodeToString(b)
	}

	var stunURLs, turnURLs []string
	if *publicHost != "" {
		_, port, _ := net.SplitHostPort(*turnAddr)
		stunURLs = []string{fmt.Sprintf("stun:%s:%s", *publicHost, port)}
		if *relayIP != "" {
			turnURLs = []string{fmt.Sprintf("turn:%s:%s?transport=udp", *publicHost, port)}
		}
	}
	if len(stunURLs) == 0 {
		stunURLs = []string{"stun:stun.l.google.com:19302"}
	}

	registry := NewRegistry(*ttl, 4, 2000)
	sessions := NewSessions(12*time.Hour, 20000, stunURLs, turnURLs, *turnSecret)
	if *publicHost != "" {
		_, port, _ := net.SplitHostPort(*signalAddr)
		sessions.signaling = net.JoinHostPort(*publicHost, port)
	}
	go func() {
		for range time.Tick(time.Minute) {
			sessions.Expire()
		}
	}()

	if *relayIP != "" {
		if _, err := StartTURN(*turnAddr, *relayIP, "edifice", *turnSecret, *relayPorts); err != nil {
			log.Fatalf("TURN: %v", err)
		}
	} else {
		log.Printf("TURN disabled (no -turn-relay-ip); players get STUN only: %v", stunURLs)
	}

	signaling := NewSignaling(sessions)
	go func() {
		log.Fatal(signaling.ListenAndServe(*signalAddr))
	}()

	api := &API{registry: registry, sessions: sessions, trustProxy: *trustProxy,
		limiter: newRateLimiter(*rateLimit, (*rateLimit+2)/3), refresh: *ttl / 3}
	srv := &http.Server{
		Addr:              *httpAddr,
		Handler:           api,
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
	}
	log.Printf("HTTP API on %s", *httpAddr)
	log.Fatal(srv.ListenAndServe())
}
