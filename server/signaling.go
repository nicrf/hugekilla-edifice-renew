package main

import (
	"bufio"
	"log"
	"net"
	"strings"
	"sync"
	"time"
)

// Signaling relays the small messages two GameNetworkingSockets peers exchange
// to open a P2P connection. Same line protocol as Valve's example
// (examples/trivial_signaling_server.py): "DESTINATION PAYLOAD" lines, forwarded
// as "SENDER PAYLOAD". Unlike the example, the first line must be
// "IDENTITY TOKEN" from a lobby session, so nobody can speak as someone else.
type Signaling struct {
	sessions    *Sessions
	mu          sync.Mutex
	clients     map[string]*sigClient
	maxLine     int
	perSecond   int
	idleTimeout time.Duration
}

type sigClient struct {
	identity string
	conn     net.Conn
	out      chan string   // never closed: senders may still hold this client
	done     chan struct{} // closed when the connection ends
}

func NewSignaling(sessions *Sessions) *Signaling {
	return &Signaling{
		sessions:    sessions,
		clients:     map[string]*sigClient{},
		maxLine:     4096,
		perSecond:   50,
		idleTimeout: 5 * time.Minute,
	}
}

func (g *Signaling) Serve(l net.Listener) error {
	for {
		conn, err := l.Accept()
		if err != nil {
			return err
		}
		go g.handle(conn)
	}
}

func printable(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] < 33 || s[i] > 126 {
			return false
		}
	}
	return len(s) > 0
}

func (g *Signaling) handle(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReaderSize(conn, g.maxLine+2)
	readLine := func(timeout time.Duration) (string, bool) {
		conn.SetReadDeadline(time.Now().Add(timeout))
		line, err := reader.ReadSlice('\n')
		if err != nil {
			return "", false // includes lines longer than maxLine
		}
		return strings.TrimRight(string(line), "\r\n"), true
	}

	hello, ok := readLine(10 * time.Second)
	if !ok {
		return
	}
	fields := strings.Fields(hello)
	if len(fields) != 2 || !g.sessions.Check(fields[0], fields[1]) {
		conn.Write([]byte("ERROR unauthorized\n"))
		return
	}
	c := &sigClient{identity: fields[0], conn: conn, out: make(chan string, 64), done: make(chan struct{})}
	g.mu.Lock()
	if old := g.clients[c.identity]; old != nil {
		old.conn.Close() // newest connection of an identity wins
	}
	g.clients[c.identity] = c
	g.mu.Unlock()
	defer func() {
		g.mu.Lock()
		if g.clients[c.identity] == c {
			delete(g.clients, c.identity)
		}
		g.mu.Unlock()
		close(c.done)
	}()
	go func() {
		for {
			select {
			case line := <-c.out:
				conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
				if _, err := conn.Write([]byte(line)); err != nil {
					conn.Close()
					return
				}
			case <-c.done:
				return
			}
		}
	}()

	window := time.Now()
	count := 0
	for {
		line, ok := readLine(g.idleTimeout)
		if !ok {
			return
		}
		if time.Since(window) > time.Second {
			window, count = time.Now(), 0
		}
		count++
		if count > g.perSecond {
			continue // over the rate limit: dropped (signaling is not guaranteed delivery)
		}
		dest, payload, found := strings.Cut(line, " ")
		if !found || !printable(dest) || !printable(payload) {
			continue
		}
		g.mu.Lock()
		target := g.clients[dest]
		g.mu.Unlock()
		if target == nil {
			continue
		}
		select {
		case target.out <- c.identity + " " + payload + "\n":
		default: // receiver not keeping up: drop
		}
	}
}

func (g *Signaling) ListenAndServe(addr string) error {
	l, err := net.Listen("tcp", addr)
	if err != nil {
		return err
	}
	log.Printf("signaling on %s", addr)
	return g.Serve(l)
}
