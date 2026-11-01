package main

import (
	"bufio"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

// The signaling relay over WebSocket (RFC 6455): GET /v1/signal on the HTTP API,
// so that it goes through the same HTTPS reverse proxy as the rest of the lobby
// and the session token is never sent in clear text. Same protocol as over TCP
// (signaling.go), one line per text frame, without its '\n': first frame
// "IDENTITY TOKEN", then "DESTINATION PAYLOAD", relayed as "SENDER PAYLOAD".
// The token goes in the first frame rather than in a header so that no access
// log of a proxy ever records it.

const wsGUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

const (
	wsContinuation = 0x0
	wsText         = 0x1
	wsBinary       = 0x2
	wsClose        = 0x8
	wsPing         = 0x9
	wsPong         = 0xA
)

var errWSProtocol = errors.New("websocket protocol error")

// headerHas tells whether a comma-separated header (Connection, Upgrade) holds
// token, ignoring case.
func headerHas(h http.Header, name, token string) bool {
	for _, v := range h.Values(name) {
		for _, t := range strings.Split(v, ",") {
			if strings.EqualFold(strings.TrimSpace(t), token) {
				return true
			}
		}
	}
	return false
}

func wsAccept(key string) string {
	sum := sha1.Sum([]byte(key + wsGUID))
	return base64.StdEncoding.EncodeToString(sum[:])
}

// readFrame reads one client frame: clients must mask them (RFC 6455 5.1), and
// payloads are at most max bytes (control frames at most 125).
func readFrame(r io.Reader, max int) (fin bool, opcode byte, payload []byte, err error) {
	var h [2]byte
	if _, err = io.ReadFull(r, h[:]); err != nil {
		return
	}
	fin, opcode = h[0]&0x80 != 0, h[0]&0x0F
	if h[0]&0x70 != 0 || h[1]&0x80 == 0 { // no extension negotiated; unmasked
		return false, 0, nil, errWSProtocol
	}
	n := uint64(h[1] & 0x7F)
	switch n {
	case 126:
		var b [2]byte
		if _, err = io.ReadFull(r, b[:]); err != nil {
			return
		}
		n = uint64(binary.BigEndian.Uint16(b[:]))
	case 127:
		var b [8]byte
		if _, err = io.ReadFull(r, b[:]); err != nil {
			return
		}
		n = binary.BigEndian.Uint64(b[:])
	}
	control := opcode&0x8 != 0
	if n > uint64(max) || (control && (n > 125 || !fin)) {
		return false, 0, nil, errWSProtocol
	}
	var mask [4]byte
	if _, err = io.ReadFull(r, mask[:]); err != nil {
		return
	}
	payload = make([]byte, n)
	if _, err = io.ReadFull(r, payload); err != nil {
		return false, 0, nil, err
	}
	for i := range payload {
		payload[i] ^= mask[i&3]
	}
	return fin, opcode, payload, nil
}

// appendFrame appends a server frame (unmasked, unfragmented).
func appendFrame(b []byte, opcode byte, payload []byte) []byte {
	b = append(b, 0x80|opcode)
	switch n := len(payload); {
	case n < 126:
		b = append(b, byte(n))
	case n <= 0xFFFF:
		b = append(b, 126, byte(n>>8), byte(n))
	default:
		b = append(b, 127)
		b = binary.BigEndian.AppendUint64(b, uint64(n))
	}
	return append(b, payload...)
}

// wsLines is a signaling connection over WebSocket.
type wsLines struct {
	conn    net.Conn
	r       *bufio.Reader
	maxLine int
	wmu     sync.Mutex // the relay goroutine and pong replies both write
}

func (c *wsLines) write(opcode byte, payload []byte) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	_, err := c.conn.Write(appendFrame(nil, opcode, payload))
	return err
}

func (c *wsLines) ReadLine(timeout time.Duration) (string, bool) {
	for {
		c.conn.SetReadDeadline(time.Now().Add(timeout))
		fin, opcode, payload, err := readFrame(c.r, c.maxLine)
		if err != nil {
			if errors.Is(err, errWSProtocol) {
				c.write(wsClose, []byte{0x03, 0xEA}) // 1002 protocol error
			}
			return "", false
		}
		switch opcode {
		case wsText, wsBinary:
			if !fin {
				return "", false // the game never fragments its short lines
			}
			return strings.TrimRight(string(payload), "\r\n"), true
		case wsPing:
			c.write(wsPong, payload)
		case wsPong:
		case wsClose:
			c.write(wsClose, nil)
			return "", false
		default: // continuation without a start, reserved opcodes
			return "", false
		}
	}
}

func (c *wsLines) WriteLine(line string) error { return c.write(wsText, []byte(line)) }
func (c *wsLines) Close() error                { return c.conn.Close() }

// ServeWebSocket upgrades GET /v1/signal and relays it like a TCP connection.
func (g *Signaling) ServeWebSocket(w http.ResponseWriter, r *http.Request) {
	key := r.Header.Get("Sec-WebSocket-Key")
	if decoded, err := base64.StdEncoding.DecodeString(key); r.Method != http.MethodGet ||
		!headerHas(r.Header, "Connection", "upgrade") || !headerHas(r.Header, "Upgrade", "websocket") ||
		err != nil || len(decoded) != 16 {
		writeError(w, http.StatusBadRequest, "websocket upgrade expected")
		return
	}
	if r.Header.Get("Sec-WebSocket-Version") != "13" {
		w.Header().Set("Sec-WebSocket-Version", "13")
		writeError(w, http.StatusUpgradeRequired, "websocket version 13 expected")
		return
	}
	hj, ok := w.(http.Hijacker)
	if !ok {
		writeError(w, http.StatusInternalServerError, "no websocket here")
		return
	}
	conn, rw, err := hj.Hijack()
	if err != nil {
		return
	}
	conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	if _, err := conn.Write([]byte("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + wsAccept(key) + "\r\n\r\n")); err != nil {
		conn.Close()
		return
	}
	g.serve(&wsLines{conn: conn, r: rw.Reader, maxLine: g.maxLine})
}
