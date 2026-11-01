// edifice-lobby: list of public Edifice games, P2P signaling for
// GameNetworkingSockets, and an optional STUN/TURN relay, in one binary.
package main

import (
	"crypto/rand"
	"crypto/tls"
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
	signalAddr := flag.String("signal", env("EDIFICE_SIGNAL", ":10000"), "signaling (TCP) address, \"off\" for the WebSocket relay of the HTTP API only")
	turnAddr := flag.String("turn", env("EDIFICE_TURN", ":3478"), "STUN/TURN (UDP) address")
	publicHost := flag.String("public-host", env("EDIFICE_PUBLIC_HOST", ""), "host name or IP players use to reach this server (for the STUN/TURN URLs)")
	relayIP := flag.String("turn-relay-ip", env("EDIFICE_TURN_RELAY_IP", ""), "public IPv4 for relayed traffic; empty disables TURN")
	relayPorts := flag.String("turn-relay-ports", env("EDIFICE_TURN_RELAY_PORTS", ""), "UDP port range for relayed traffic, e.g. 49160-49200 (needed behind Docker port mapping)")
	turnSecret := flag.String("turn-secret", env("EDIFICE_TURN_SECRET", ""), "shared secret for TURN credentials (random if empty)")
	trustProxy := flag.Bool("trust-proxy", os.Getenv("EDIFICE_TRUST_PROXY") == "1", "take the client IP from X-Forwarded-For (behind a reverse proxy)")
	trustedProxies := flag.String("trusted-proxies", env("EDIFICE_TRUSTED_PROXIES", ""), "with -trust-proxy, comma-separated IPs/CIDRs of the reverse proxies (empty: just one, the peer)")
	allowPrivate := flag.Bool("allow-private", os.Getenv("EDIFICE_ALLOW_PRIVATE") == "1", "list games announced from private/loopback addresses (LAN and tests)")
	publicURL := flag.String("public-url", env("EDIFICE_PUBLIC_URL", ""), "URL players reach the HTTP API at, e.g. https://lobby.example.org (empty: taken from each request)")
	tlsCert := flag.String("tls-cert", env("EDIFICE_TLS_CERT", ""), "serve the HTTP API over HTTPS with this certificate (PEM; without a TLS reverse proxy)")
	tlsKey := flag.String("tls-key", env("EDIFICE_TLS_KEY", ""), "...and this key")
	ttl := flag.Duration("ttl", 60*time.Second, "a game disappears when its host hasn't refreshed it for this long")
	rateLimit := flag.Int("rate-limit", 30, "write requests and game code lookups allowed per minute and per IP (burst: a third)")
	healthcheck := flag.Bool("healthcheck", false, "check that the HTTP API of a running instance answers, then exit (for Docker)")
	flag.Parse()

	if *healthcheck {
		_, port, _ := net.SplitHostPort(*httpAddr)
		scheme := "http"
		client := http.Client{Timeout: 3 * time.Second}
		if *tlsCert != "" {
			// our own certificate, on 127.0.0.1: only whether it answers matters
			scheme = "https"
			client.Transport = &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
		}
		resp, err := client.Get(scheme + "://127.0.0.1:" + port + "/healthz")
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
	tcpSignal := *signalAddr != "off" && *signalAddr != ""
	if *publicHost != "" && tcpSignal {
		_, port, _ := net.SplitHostPort(*signalAddr)
		sessions.signaling = net.JoinHostPort(*publicHost, port)
	}
	proxies, err := ParseProxies(*trustedProxies)
	if err != nil {
		log.Fatalf("-trusted-proxies: %v", err)
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
	if tcpSignal {
		go func() {
			log.Fatal(signaling.ListenAndServe(*signalAddr))
		}()
	} else {
		log.Printf("TCP signaling off: WebSocket relay (GET /v1/signal) only")
	}

	api := &API{registry: registry, sessions: sessions, signaling: signaling, trustProxy: *trustProxy,
		trustedProxies: proxies, allowPrivate: *allowPrivate, publicURL: *publicURL,
		limiter: newRateLimiter(*rateLimit, (*rateLimit+2)/3), refresh: *ttl / 3}
	srv := &http.Server{
		Addr:              *httpAddr,
		Handler:           api,
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
	}
	if *tlsCert != "" {
		log.Printf("HTTPS API on %s", *httpAddr)
		log.Fatal(srv.ListenAndServeTLS(*tlsCert, *tlsKey))
	}
	log.Printf("HTTP API on %s", *httpAddr)
	log.Fatal(srv.ListenAndServe())
}
