// A WebSocket client for the lobby page's checks (web_lobby_server_check.js, web_lobby_handoff_check.js): over a plain socket, so that it is the same on every node (the global WebSocket arrived in node 22)
'use strict';
const net = require('net');
const crypto = require('crypto');

// It has the parts of the browser's WebSocket that the client uses (binaryType, send, close, onopen, onmessage, onclose, onerror). A server frame is one message per binary frame; a client frame is masked.
class MiniWebSocket {
    constructor(url) {
        const m = /^ws:\/\/([^:/]+):(\d+)(\/.*)?$/.exec(url);
        if (!m) throw new Error('ws://host:port/path only: ' + url);
        this.readyState = 0;                                           // 0 connecting, 1 open, 2 closing, 3 closed
        this.binaryType = 'arraybuffer';
        this.onopen = this.onmessage = this.onclose = this.onerror = null;
        this.buffer = Buffer.alloc(0);
        this.head = null;                                              // the answer to the handshake until its blank line has come
        this.pieces = [];
        const key = crypto.randomBytes(16).toString('base64');
        this.socket = net.connect(Number(m[2]), m[1]);
        this.socket.on('connect', () => this.socket.write('GET ' + (m[3] || '/') + ' HTTP/1.1\r\nHost: ' + m[1] + ':' + m[2] + '\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ' + key + '\r\nSec-WebSocket-Version: 13\r\n\r\n'));
        this.socket.on('data', (data) => this.take(data));
        this.socket.on('error', () => { if (this.onerror) this.onerror({}); });
        this.socket.on('close', () => this.finish());
    }
    finish() {
        if (this.readyState === 3) return;
        this.readyState = 3;
        if (this.onclose) this.onclose({ code: 1006 });
    }
    take(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        if (this.head === null) {
            const end = this.buffer.indexOf('\r\n\r\n');
            if (end < 0) return;
            this.head = this.buffer.slice(0, end).toString('latin1');
            this.buffer = this.buffer.slice(end + 4);
            if (!/^HTTP\/1\.1 101 /.test(this.head)) { this.socket.destroy(); return; }
            this.readyState = 1;
            if (this.onopen) this.onopen({});
        }
        for (;;) {
            const b = this.buffer;
            if (b.length < 2) return;
            let length = b[1] & 127;
            let at = 2;
            if (length === 126) { if (b.length < 4) return; length = b.readUInt16BE(2); at = 4; }
            else if (length === 127) { if (b.length < 10) return; length = Number(b.readBigUInt64BE(2)); at = 10; }
            if ((b[1] & 128) !== 0) { this.socket.destroy(); return; }          // (a server does not mask)
            if (b.length < at + length) return;
            const fin = (b[0] & 128) !== 0;
            const opcode = b[0] & 15;
            const payload = Buffer.from(b.slice(at, at + length));
            this.buffer = b.slice(at + length);
            if (opcode === 8) { this.sendFrame(8, payload.slice(0, 2)); this.readyState = 2; this.socket.end(); return; }
            if (opcode === 9) { this.sendFrame(10, payload); continue; }
            if (opcode === 10) continue;
            this.pieces.push(payload);
            if (!fin) continue;
            const whole = Buffer.concat(this.pieces);
            this.pieces = [];
            if (opcode === 2 || opcode === 0) {
                if (this.onmessage) this.onmessage({ data: whole.buffer.slice(whole.byteOffset, whole.byteOffset + whole.length) });
            }
        }
    }
    sendFrame(opcode, payload) {
        if (this.socket.destroyed || !this.socket.writable) return;
        const mask = crypto.randomBytes(4);
        const n = payload.length;
        const head = n < 126 ? Buffer.from([128 | opcode, 128 | n]) : n < 65536 ? Buffer.from([128 | opcode, 128 | 126, n >> 8, n & 255]) : null;
        if (head === null) throw new Error('a frame of 64 KiB or more');
        const masked = Buffer.alloc(n);
        for (let i = 0; i < n; i++) masked[i] = payload[i] ^ mask[i & 3];
        this.socket.write(Buffer.concat([head, mask, masked]));
    }
    send(bytes) {
        if (this.readyState !== 1) throw new Error('send on a socket that is not open');
        this.sendFrame(2, Buffer.from(bytes));
    }
    close() {
        if (this.readyState >= 2) return;
        this.readyState = 2;
        this.sendFrame(8, Buffer.from([3, 232]));                      // 1000
        this.socket.end();
    }
}

module.exports = MiniWebSocket;
