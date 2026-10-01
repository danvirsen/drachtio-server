const test = require('tape');
const net = require('net');
const config = require('./scripts/config');
const {start, stop} = require('./testbed');

// drachtio reads client frames 8 KB at a time; frames larger than that, sent back to back,
// must reach it intact however they are split across reads.

const frame = (msg) => `${Buffer.byteLength(msg)}#${msg}`;

// connect a raw client and collect the frames drachtio sends back
const connect = () => new Promise((resolve, reject) => {
  const {host, port} = config.drachtio.connectOpts;
  const sock = net.connect({host, port}, () => resolve(sock));
  sock.on('error', reject);
  let buf = '';
  sock.frames = [];
  sock.on('data', (data) => {
    buf += data.toString();
    for (;;) {
      const hash = buf.indexOf('#');
      if (hash < 0) break;
      const len = parseInt(buf.slice(0, hash));
      if (buf.length < hash + 1 + len) break;
      sock.frames.push(buf.slice(hash + 1, hash + 1 + len));
      buf = buf.slice(hash + 1 + len);
    }
    sock.emit('frames');
  });
});

// resolve once `count` frames have arrived; reject if drachtio closes the connection first
const waitForFrames = (sock, count, ms = 5000) => new Promise((resolve, reject) => {
  const check = () => {
    if (sock.frames.length >= count) { done(); resolve(sock.frames); }
  };
  const onClose = () => { done(); reject(new Error(`connection closed after ${sock.frames.length} frames`)); };
  const timer = setTimeout(() => { done(); reject(new Error(`timed out after ${ms}ms`)); }, ms);
  const done = () => {
    clearTimeout(timer);
    sock.removeListener('frames', check);
    sock.removeListener('close', onClose);
  };
  sock.on('frames', check);
  sock.on('close', onClose);
  check();
});

test('pipelined frames larger than one read are all processed', async(t) => {
  let sock;
  try {
    await start(null, []);
    sock = await connect();
    sock.write(frame(`auth|authenticate|${config.drachtio.connectOpts.secret}`));
    const [auth] = await waitForFrames(sock, 1);
    t.ok(auth.includes('|response|auth|OK'), 'client authenticated');

    const sizes = [9000, 9000, 20000];
    sock.write(sizes.map((n, i) => frame(`ping${i}|ping|${'x'.repeat(n)}`)).join(''));
    const frames = (await waitForFrames(sock, 1 + sizes.length)).slice(1);
    sizes.forEach((n, i) => {
      t.ok(frames[i].includes(`|response|ping${i}|OK|pong`), `pong for the ${n}-byte ping ${i}`);
    });
  } catch (err) {
    t.fail(`failed with error ${err}`);
  }
  if (sock) sock.destroy();
  await stop();
  t.end();
});
