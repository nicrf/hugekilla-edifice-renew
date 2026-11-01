// WebRTC of the web build (roadmap M14 task 3), a --js-library for
// src/net/webrtclink.cpp: the browser has neither UDP nor GameNetworkingSockets, so
// it joins a host over a WebRTC DataChannel, and hosts by taking the offers of
// browsers and desktop clients. Only the browser's objects live here: the WebSocket
// to the lobby's signaling relay (wss://.../v1/signal, one line a text frame) and
// the RTCPeerConnections with their DataChannel. What goes over the relay is read
// and written in C++ (signaling.cpp, tested and fuzzed there), and everything is
// polled by the game every frame: no callback into the game.
addToLibrary({
	$HKRTC: {
		ws: null,          // the WebSocket, while open or opening
		url: '',
		hello: '',         // "IDENTITY TOKEN": first frame, never logged
		out: [],           // lines waiting for the WebSocket to open
		lines: [],         // lines from the relay, for the game
		conns: {},
		next: 1,
		redial: 0,         // when to open the WebSocket again after it closed
		backoff: 1000,
		lastsent: 0,
		hosting: 0,
		watching: false,
		timers: false,     // the main loop runs on timers (hidden while hosting)
		MAXLINES: 256,
		MAXQUEUE: 512,     // datagrams waiting per connection: then the oldest go
		MAXBUFFERED: 262144,
		KEEPALIVE: 60000,  // the relay drops a connection silent for 5 minutes
		log: function (text) { console.log('webrtc: ' + text); },
		send: function (line) {
			HKRTC.ws.send(line);
			HKRTC.lastsent = Date.now();
		},
		dial: function () {
			if (HKRTC.ws || !HKRTC.url) return;
			var ws;
			try {
				ws = new WebSocket(HKRTC.url);
			} catch (e) {
				HKRTC.log("can't open " + HKRTC.url + ': ' + e);
				return;
			}
			HKRTC.ws = ws;
			ws.onopen = function () {
				HKRTC.log('signaling: connected to ' + HKRTC.url);
				HKRTC.backoff = 1000;
				HKRTC.send(HKRTC.hello);
				while (HKRTC.out.length) HKRTC.send(HKRTC.out.shift());
			};
			ws.onmessage = function (e) {
				if (typeof e.data !== 'string' || e.data.length > 4000) return;
				if (HKRTC.lines.length >= HKRTC.MAXLINES) HKRTC.lines.shift();
				HKRTC.lines.push(e.data);
			};
			ws.onclose = function () {
				if (HKRTC.ws !== ws) return;
				// opened again by the next line to send, or by the game's polling (a
				// host must stay reachable): after 1 s, then longer, up to a minute
				HKRTC.ws = null;
				HKRTC.redial = Date.now() + HKRTC.backoff;
				HKRTC.backoff = Math.min(HKRTC.backoff * 2, 60000);
			};
			ws.onerror = function () {
				HKRTC.log('signaling: ' + HKRTC.url + ' failed');
			};
		},
		// the DataChannel of connection c: ours (offerer) or the peer's (answerer)
		take: function (c, dc) {
			c.dc = dc;
			dc.binaryType = 'arraybuffer';
			dc.onmessage = function (e) {
				if (!(e.data instanceof ArrayBuffer)) return;
				if (c.rx.length >= HKRTC.MAXQUEUE) c.rx.shift();
				c.rx.push(new Uint8Array(e.data));
			};
		},
		// a connection with the session's STUN/TURN servers (ice: RTCIceServer[] as
		// JSON text); relay: only TURN relay candidates
		peer: function (ice, relay) {
			var servers = [];
			try {
				servers = JSON.parse(ice);
			} catch (e) {}
			var c = { local: [], held: [], offered: false, rx: [], chain: Promise.resolve(), failed: false, dc: null };
			c.pc = new RTCPeerConnection({ iceServers: servers, iceTransportPolicy: relay ? 'relay' : 'all' });
			c.pc.onicecandidate = function (e) {
				if (!e.candidate || !e.candidate.candidate) return;   // the end of gathering
				var s = { k: 'c', a: e.candidate.candidate, b: e.candidate.sdpMid || '' };
				(c.offered ? c.local : c.held).push(s);
			};
			c.pc.onconnectionstatechange = function () {
				HKRTC.log('connection ' + c.pc.connectionState);
			};
			return c;
		},
		// the offer or answer goes first, then the candidates gathered meanwhile
		// (trickle ICE)
		described: function (c, kind) {
			c.local.push({ k: kind, a: c.pc.localDescription.sdp, b: '' });
			c.offered = true;
			while (c.held.length) c.local.push(c.held.shift());
		},
		add: function (c) {
			var h = HKRTC.next++;
			HKRTC.conns[h] = c;
			return h;
		},
	},

	hk_rtc_ws_open__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_ws_open: function (url, hello) {
		var u = UTF8ToString(url), h = UTF8ToString(hello);
		if (HKRTC.ws && (HKRTC.url !== u || HKRTC.hello !== h)) {
			var old = HKRTC.ws;
			HKRTC.ws = null;
			old.close();
		}
		HKRTC.url = u;
		HKRTC.hello = h;
		HKRTC.dial();
	},

	hk_rtc_ws_send__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_ws_send: function (line) {
		var l = UTF8ToString(line);
		if (HKRTC.ws && HKRTC.ws.readyState === 1) {
			HKRTC.send(l);
			return;
		}
		if (HKRTC.out.length >= 64) HKRTC.out.shift();
		HKRTC.out.push(l);
		HKRTC.dial();
	},

	// the next line from the relay: its length (copied with a NUL), 0 if none,
	// -1 if it didn't fit (dropped). Polled every frame: also dials the relay again
	// once it closed, and keeps it open.
	hk_rtc_ws_recv__deps: ['$HKRTC', '$stringToUTF8', '$lengthBytesUTF8'],
	hk_rtc_ws_recv: function (buf, max) {
		var now = Date.now();
		if (!HKRTC.ws && HKRTC.url && now >= HKRTC.redial) {
			HKRTC.redial = now + HKRTC.backoff;
			HKRTC.dial();
		}
		// a line without destination, which the relay reads and drops
		if (HKRTC.ws && HKRTC.ws.readyState === 1 && now - HKRTC.lastsent > HKRTC.KEEPALIVE) HKRTC.send('keepalive');
		if (!HKRTC.lines.length) return 0;
		var l = HKRTC.lines.shift(), n = lengthBytesUTF8(l);
		if (n + 1 > max) return -1;
		stringToUTF8(l, buf, max);
		return n;
	},

	hk_rtc_ws_close__deps: ['$HKRTC'],
	hk_rtc_ws_close: function () {
		var ws = HKRTC.ws;
		HKRTC.ws = null;
		HKRTC.url = '';   // never dialed again
		if (ws) ws.close();
		HKRTC.out = [];
		HKRTC.lines = [];
	},

	// a connection to a host: an offer for a DataChannel "edifice", unordered and
	// never retransmitted (Sock has its own reliable channel). Returns its handle.
	hk_rtc_connect__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_connect: function (ice, relay) {
		var c;
		try {
			c = HKRTC.peer(UTF8ToString(ice), relay);
			HKRTC.take(c, c.pc.createDataChannel('edifice', { ordered: false, maxRetransmits: 0 }));
		} catch (e) {
			HKRTC.log("can't create a connection: " + e);
			return 0;
		}
		c.pc.createOffer().then(function (offer) {
			return c.pc.setLocalDescription(offer);
		}).then(function () {
			HKRTC.described(c, 'o');
		}).catch(function (e) {
			HKRTC.log('offer failed: ' + e);
			c.failed = true;
		});
		return HKRTC.add(c);
	},

	// a host takes an offer (a browser's or a desktop client's: clients offer):
	// its answer, then its candidates, go out through hk_rtc_local, and the
	// client's DataChannel "edifice" becomes the connection's. Returns its handle.
	hk_rtc_accept__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_accept: function (ice, relay, offer) {
		var c, sdp = UTF8ToString(offer);
		try {
			c = HKRTC.peer(UTF8ToString(ice), relay);
		} catch (e) {
			HKRTC.log("can't create a connection: " + e);
			return 0;
		}
		c.pc.ondatachannel = function (e) {
			if (!c.dc && e.channel.label === 'edifice') HKRTC.take(c, e.channel);
		};
		// the client's candidates wait in the chain for the offer (hk_rtc_remote)
		c.chain = c.pc.setRemoteDescription({ type: 'offer', sdp: sdp }).then(function () {
			return c.pc.createAnswer();
		}).then(function (answer) {
			return c.pc.setLocalDescription(answer);
		}).then(function () {
			HKRTC.described(c, 'a');
		}).catch(function (e) {
			HKRTC.log('answer failed: ' + e);
			c.failed = true;
		});
		return HKRTC.add(c);
	},

	// the next signal of connection h for the other side: 1 (kind 'o', 'a' or 'c',
	// text, and for a candidate its mid), 0 if none, -1 if one didn't fit (dropped)
	hk_rtc_local__deps: ['$HKRTC', '$stringToUTF8', '$lengthBytesUTF8'],
	hk_rtc_local: function (h, kind, a, amax, b, bmax) {
		var c = HKRTC.conns[h];
		if (!c || !c.local.length) return 0;
		var s = c.local.shift();
		if (lengthBytesUTF8(s.a) + 1 > amax || lengthBytesUTF8(s.b) + 1 > bmax) return -1;
		HEAP8[kind] = s.k.charCodeAt(0);
		stringToUTF8(s.a, a, amax);
		stringToUTF8(s.b, b, bmax);
		return 1;
	},

	// a signal of the other side for connection h (checked by signaling.cpp): the
	// host's answer ('a'), or a candidate of either side ('c', with its mid)
	hk_rtc_remote__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_remote: function (h, kind, a, b) {
		var c = HKRTC.conns[h];
		if (!c) return;
		var text = UTF8ToString(a), mid = UTF8ToString(b);
		// in order: a candidate is added once the descriptions are set
		c.chain = c.chain.then(function () {
			if (kind === 97) return c.pc.setRemoteDescription({ type: 'answer', sdp: text });   // 'a'
			if (kind === 99) return c.pc.addIceCandidate({ candidate: text, sdpMid: mid || '0' });   // 'c'
		}).catch(function (e) {
			HKRTC.log((kind === 97 ? 'answer' : 'candidate') + ' refused: ' + e);
		});
	},

	// 1 open, 0 still connecting, -1 failed or closed
	hk_rtc_state__deps: ['$HKRTC'],
	hk_rtc_state: function (h) {
		var c = HKRTC.conns[h];
		if (!c || c.failed) return -1;
		var pcs = c.pc.connectionState, dcs = c.dc ? c.dc.readyState : 'connecting';
		if (pcs === 'failed' || pcs === 'closed' || dcs === 'closing' || dcs === 'closed') return -1;
		return dcs === 'open' ? 1 : 0;
	},

	// 1 if the connection goes through a TURN relay, 0 if not or not known yet
	hk_rtc_relayed__deps: ['$HKRTC'],
	hk_rtc_relayed: function (h) {
		var c = HKRTC.conns[h];
		if (!c) return 0;
		if (c.relayed === undefined) {
			c.relayed = 0;
			c.pc.getStats().then(function (stats) {
				var pair = null, kind = {};
				stats.forEach(function (s) {
					if (s.type === 'local-candidate' || s.type === 'remote-candidate') kind[s.id] = s.candidateType;
					if (s.type === 'candidate-pair' && s.nominated && s.state === 'succeeded') pair = s;
				});
				c.relayed = pair && (kind[pair.localCandidateId] === 'relay' || kind[pair.remoteCandidateId] === 'relay') ? 1 : 0;
			}).catch(function () {});
		}
		return c.relayed;
	},

	hk_rtc_send__deps: ['$HKRTC'],
	hk_rtc_send: function (h, data, size) {
		var c = HKRTC.conns[h];
		if (!c || !c.dc || c.dc.readyState !== 'open' || c.dc.bufferedAmount > HKRTC.MAXBUFFERED) return -1;
		try {
			c.dc.send(HEAPU8.slice(data, data + size));   // a copy: the heap may grow
		} catch (e) {
			return -1;
		}
		return size;
	},

	// the next datagram of connection h: its size, 0 if none, -1 if it didn't fit
	hk_rtc_recv__deps: ['$HKRTC'],
	hk_rtc_recv: function (h, data, max) {
		var c = HKRTC.conns[h];
		if (!c || !c.rx.length) return 0;
		var m = c.rx.shift();
		if (m.length > max) return -1;
		HEAPU8.set(m, data);
		return m.length;
	},

	hk_rtc_close__deps: ['$HKRTC'],
	hk_rtc_close: function (h) {
		var c = HKRTC.conns[h];
		if (!c) return;
		delete HKRTC.conns[h];
		try {
			if (c.dc) c.dc.close();
			c.pc.close();
		} catch (e) {}
	},

	// hosting an Internet game in this tab (1), or not any more (0). A browser
	// stops drawing a hidden tab (requestAnimationFrame), which would stop the
	// server and drop its players: a banner asks to keep the tab open and visible,
	// and while it is hidden the game runs on timers instead (the browser throttles
	// them to about once a second: slow, but the players stay connected).
	hk_web_hosting__deps: ['$HKRTC', 'emscripten_set_main_loop_timing'],
	hk_web_hosting: function (on) {
		if (typeof document === 'undefined') return;
		HKRTC.hosting = on;
		var b = document.getElementById('hostbanner');
		if (!b && on) {
			b = document.createElement('div');
			b.id = 'hostbanner';
			b.textContent = 'Hosting an Internet game: keep this tab open and visible';
			b.style.cssText = 'position:fixed;top:0;left:50%;transform:translateX(-50%);z-index:5;' +
				'padding:3px 12px;font:13px sans-serif;color:#fff;background:rgba(150,90,0,.85);' +
				'border-radius:0 0 6px 6px;pointer-events:none;white-space:nowrap';
			document.body.appendChild(b);
		}
		if (b) b.style.display = on ? '' : 'none';
		var timing = function () {
			var hidden = !!(document.hidden && HKRTC.hosting);
			if (hidden === HKRTC.timers) return;
			HKRTC.timers = hidden;
			HKRTC.log(hidden ? 'tab hidden while hosting: the game slows down until it is shown again' : 'tab shown again');
			if (hidden) _emscripten_set_main_loop_timing(0, 16);   // EM_TIMING_SETTIMEOUT, 16 ms
			else _emscripten_set_main_loop_timing(1, 1);           // EM_TIMING_RAF, every frame
		};
		if (!HKRTC.watching) {
			HKRTC.watching = true;
			document.addEventListener('visibilitychange', timing);
		}
		timing();
	},
});
