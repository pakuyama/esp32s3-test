// MJPEG relay for XIAO ESP32-S3 camera
// - POST /push  (public, Windows firewall opens PORT): board uploads one JPEG per request, X-Token required
// - GET  /stream (loopback only; Tailscale Serve exposes it to the tailnet): multipart MJPEG to any number of viewers
// The push response carries X-Viewers so the board only streams at full rate while someone watches.
// Run: node relay/relay.mjs   (token in relay/token.txt, same value as RELAY_TOKEN in SupabaseLED/secrets.h)
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';

const PORT = 8090;
const TOKEN = readFileSync(new URL('./token.txt', import.meta.url), 'utf8').trim();
const BOUNDARY = 'frame';
const viewers = new Set();
let frames = 0;

function pushFrame(req, res) {
  if (req.headers['x-token'] !== TOKEN) {
    res.writeHead(403).end();
    req.resume();
    return;
  }
  const chunks = [];
  req.on('data', c => chunks.push(c));
  req.on('end', () => {
    const jpg = Buffer.concat(chunks);
    const head = `--${BOUNDARY}\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpg.length}\r\n\r\n`;
    for (const v of viewers) {
      // Skip a viewer whose socket is still backed up rather than queueing stale frames
      if (v.writableNeedDrain) continue;
      v.write(head);
      v.write(jpg);
      v.write('\r\n');
    }
    frames++;
    res.writeHead(200, { 'X-Viewers': String(viewers.size), 'Content-Length': 0 }).end();
  });
}

function stream(req, res) {
  res.writeHead(200, {
    'Content-Type': `multipart/x-mixed-replace; boundary=${BOUNDARY}`,
    'Cache-Control': 'no-store',
    'Access-Control-Allow-Origin': '*',
  });
  res.flushHeaders();  // let the viewer (and Tailscale's proxy) see the response before the first frame
  viewers.add(res);
  req.on('close', () => viewers.delete(res));
}

createServer((req, res) => {
  const path = req.url.split('?')[0];
  // Only /push is public; viewing must come through Tailscale Serve, which connects from loopback
  const local = ['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(req.socket.remoteAddress);
  if (!local && path !== '/push') return res.writeHead(404).end();
  if (req.method === 'POST' && path === '/push') return pushFrame(req, res);
  if (req.method === 'GET' && path === '/stream') return stream(req, res);
  if (req.method === 'GET' && path === '/status') {
    return res.writeHead(200, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' })
      .end(JSON.stringify({ viewers: viewers.size, frames }));
  }
  res.writeHead(404).end();
}).listen(PORT, () => console.log(`relay on port ${PORT}`));

setInterval(() => {
  if (frames) console.log(`${new Date().toLocaleTimeString()} ${frames / 10} fps, ${viewers.size} viewer(s)`);
  frames = 0;
}, 10000);
