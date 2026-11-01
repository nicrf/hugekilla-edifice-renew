// WebRTC of the web build (roadmap M14 task 3), a --js-library for
// src/net/webrtclink.cpp: the browser has neither UDP nor GameNetworkingSockets, so
// it joins a native host over a WebRTC DataChannel. Only the browser's objects live
// here: the WebSocket to the lobby's signaling relay (wss://.../v1/signal, one line
// a text frame) and the RTCPeerConnections with their DataChannel. What goes over
// the relay is read and written in C++ (signaling.cpp, tested and fuzzed there),
// and everything is polled by the game every frame: no callback into the game.
addToLibrary({
	$HKRTC: {
		ws: null,          // the WebSocket, while open or opening
		url: '',
		hello: '',         // "IDENTITY TOKEN": first frame, never logged
		out: [],           // lines waiting for the WebSocket to open
		lines: [],         // lines from the relay, for the game
		conns: {},
		next: 1,
		MAXLINES: 256,
		MAXQUEUE: 512,     // datagrams waiting per connection: then the oldest go
		MAXBUFFERED: 262144,
		log: function (text) { console.log('webrtc: ' + text); },
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
				ws.send(HKRTC.hello);
				while (HKRTC.out.length) ws.send(HKRTC.out.shift());
			};
			ws.onmessage = function (e) {
				if (typeof e.data !== 'string' || e.data.length > 4000) return;
				if (HKRTC.lines.length >= HKRTC.MAXLINES) HKRTC.lines.shift();
				HKRTC.lines.push(e.data);
			};
			ws.onclose = function () {
				if (HKRTC.ws === ws) HKRTC.ws = null;   // opened again by the next line to send
			};
			ws.onerror = function () {
				HKRTC.log('signaling: ' + HKRTC.url + ' failed');
			};
		},
	},

	hk_rtc_ws_open__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_ws_open: function (url, hello) {
		var u = UTF8ToString(url), h = UTF8ToString(hello);
		if (HKRTC.ws && (HKRTC.url !== u || HKRTC.hello !== h)) {
			HKRTC.ws.close();
			HKRTC.ws = null;
		}
		HKRTC.url = u;
		HKRTC.hello = h;
		HKRTC.dial();
	},

	hk_rtc_ws_send__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_ws_send: function (line) {
		var l = UTF8ToString(line);
		if (HKRTC.ws && HKRTC.ws.readyState === 1) {
			HKRTC.ws.send(l);
			return;
		}
		if (HKRTC.out.length >= 64) HKRTC.out.shift();
		HKRTC.out.push(l);
		HKRTC.dial();
	},

	// the next line from the relay: its length (copied with a NUL), 0 if none,
	// -1 if it didn't fit (dropped)
	hk_rtc_ws_recv__deps: ['$HKRTC', '$stringToUTF8', '$lengthBytesUTF8'],
	hk_rtc_ws_recv: function (buf, max) {
		if (!HKRTC.lines.length) return 0;
		var l = HKRTC.lines.shift(), n = lengthBytesUTF8(l);
		if (n + 1 > max) return -1;
		stringToUTF8(l, buf, max);
		return n;
	},

	hk_rtc_ws_close__deps: ['$HKRTC'],
	hk_rtc_ws_close: function () {
		if (HKRTC.ws) HKRTC.ws.close();
		HKRTC.ws = null;
		HKRTC.out = [];
		HKRTC.lines = [];
	},

	// a connection: an offer for a DataChannel "edifice", unordered and never
	// retransmitted (Sock has its own reliable channel). ice: RTCIceServer[] as
	// JSON; relay: only TURN relay candidates. Returns its handle.
	hk_rtc_connect__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_connect: function (ice, relay) {
		var servers = [];
		try {
			servers = JSON.parse(UTF8ToString(ice));
		} catch (e) {}
		var c = { local: [], held: [], offered: false, rx: [], chain: Promise.resolve(), failed: false };
		try {
			c.pc = new RTCPeerConnection({ iceServers: servers, iceTransportPolicy: relay ? 'relay' : 'all' });
			c.dc = c.pc.createDataChannel('edifice', { ordered: false, maxRetransmits: 0 });
		} catch (e) {
			HKRTC.log("can't create a connection: " + e);
			return 0;
		}
		c.dc.binaryType = 'arraybuffer';
		c.dc.onmessage = function (e) {
			if (!(e.data instanceof ArrayBuffer)) return;
			if (c.rx.length >= HKRTC.MAXQUEUE) c.rx.shift();
			c.rx.push(new Uint8Array(e.data));
		};
		c.pc.onicecandidate = function (e) {
			if (!e.candidate || !e.candidate.candidate) return;   // the end of gathering
			var s = { k: 'c', a: e.candidate.candidate, b: e.candidate.sdpMid || '' };
			(c.offered ? c.local : c.held).push(s);
		};
		c.pc.onconnectionstatechange = function () {
			HKRTC.log('connection ' + c.pc.connectionState);
		};
		c.pc.createOffer().then(function (offer) {
			return c.pc.setLocalDescription(offer);
		}).then(function () {
			// the offer before the candidates gathered meanwhile (trickle ICE)
			c.local.push({ k: 'o', a: c.pc.localDescription.sdp, b: '' });
			c.offered = true;
			while (c.held.length) c.local.push(c.held.shift());
		}).catch(function (e) {
			HKRTC.log('offer failed: ' + e);
			c.failed = true;
		});
		var h = HKRTC.next++;
		HKRTC.conns[h] = c;
		return h;
	},

	// the next signal of connection h for the host: 1 (kind 'o' or 'c', text, and
	// for a candidate its mid), 0 if none, -1 if one didn't fit (dropped)
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

	// a signal of the host for connection h (checked by signaling.cpp): its answer
	// ('a') or one of its candidates ('c', with its mid)
	hk_rtc_remote__deps: ['$HKRTC', '$UTF8ToString'],
	hk_rtc_remote: function (h, kind, a, b) {
		var c = HKRTC.conns[h];
		if (!c) return;
		var text = UTF8ToString(a), mid = UTF8ToString(b);
		// in order: a candidate is added once the answer is set
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
		var pcs = c.pc.connectionState, dcs = c.dc.readyState;
		if (pcs === 'failed' || pcs === 'closed' || dcs === 'closing' || dcs === 'closed') return -1;
		return dcs === 'open' ? 1 : 0;
	},

	hk_rtc_send__deps: ['$HKRTC'],
	hk_rtc_send: function (h, data, size) {
		var c = HKRTC.conns[h];
		if (!c || c.dc.readyState !== 'open' || c.dc.bufferedAmount > HKRTC.MAXBUFFERED) return -1;
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
			c.dc.close();
			c.pc.close();
		} catch (e) {}
	},
});
