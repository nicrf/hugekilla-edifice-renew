package main

import (
	"fmt"
	"log"
	"net"

	"github.com/pion/turn/v4"
)

// StartTURN runs a STUN/TURN server (relay for players whose connection can't
// go peer to peer). Credentials are the time-limited ones handed out with lobby
// sessions (TURN REST API format, see turnCredentials).
// relayPorts limits the UDP ports used for relayed traffic ("49160-49200");
// empty lets the system pick any port (fine without Docker port mapping).
func StartTURN(addr, relayIP, realm, secret, relayPorts string) (*turn.Server, error) {
	ip := net.ParseIP(relayIP)
	if ip == nil {
		return nil, fmt.Errorf("invalid TURN relay IP %q", relayIP)
	}
	var generator turn.RelayAddressGenerator = &turn.RelayAddressGeneratorStatic{RelayAddress: ip, Address: "0.0.0.0"}
	if relayPorts != "" {
		var min, max uint16
		if _, err := fmt.Sscanf(relayPorts, "%d-%d", &min, &max); err != nil || min == 0 || max < min {
			return nil, fmt.Errorf("invalid TURN relay port range %q (expected like 49160-49200)", relayPorts)
		}
		generator = &turn.RelayAddressGeneratorPortRange{RelayAddress: ip, Address: "0.0.0.0", MinPort: min, MaxPort: max}
	}
	conn, err := net.ListenPacket("udp4", addr)
	if err != nil {
		return nil, err
	}
	server, err := turn.NewServer(turn.ServerConfig{
		Realm:       realm,
		AuthHandler: turn.LongTermTURNRESTAuthHandler(secret, nil),
		PacketConnConfigs: []turn.PacketConnConfig{{
			PacketConn:            conn,
			RelayAddressGenerator: generator,
		}},
	})
	if err != nil {
		conn.Close()
		return nil, err
	}
	log.Printf("STUN/TURN on %s (relay address %s)", addr, relayIP)
	return server, nil
}
