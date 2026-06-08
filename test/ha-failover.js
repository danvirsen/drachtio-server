/*
 * End-to-end test for drachtio-server FAILOVER recovery (Phase 3).
 *
 * Unlike ha-reattach.js (the application restarts), here the SERVER fails over while the
 * application stays up:
 *
 *   1. drachtio A runs with --ha-enabled and writes dialog state to redis as the call is set up.
 *   2. The application (which stays alive throughout) answers an INVITE from sipp.
 *   3. A is killed and B is started with --recover-on-start: B loads the dialog from redis and
 *      recreates the Sofia leg before serving SIP.
 *   4. The application auto-reconnects to B (same admin port) and re-attaches its live dialog.
 *   5. sipp (holding the call open) sends a BYE; B matches the recovered leg and routes it to
 *      the re-attached application, which sees the dialog 'destroy'. sipp gets its 200 OK.
 *
 * Requirements (skips cleanly if absent):
 *   - redis reachable at HA_REDIS_HOST:HA_REDIS_PORT (default 127.0.0.1:6379)
 *   - a drachtio-srf with reattach support (the feature/ha branch); set HA_SRF_PATH
 *   - the locally-built ../build/drachtio (carries --ha-enabled / --recover-on-start) and sipp
 *
 * Run:  cd test && HA_SRF_PATH=../../drachtio-srf node ./ha-failover.js
 */
const test = require('blue-tape');
const net = require('net');
const { execSync } = require('child_process');
const { start, stop } = require('./testbed');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');

let Srf;
try { Srf = require(process.env.HA_SRF_PATH || 'drachtio-srf'); } catch (e) { /* not installed */ }
const HaApp = require('./scripts/ha-app');

const REDIS_HOST = process.env.HA_REDIS_HOST || '127.0.0.1';
const REDIS_PORT = parseInt(process.env.HA_REDIS_PORT || '6379', 10);
const HA_ARGS = ['--ha-enabled', '--ha-redis-address', REDIS_HOST, '--ha-redis-port', String(REDIS_PORT)];

const haSupported = !!Srf &&
  typeof Srf.prototype.enableDialogPersistence === 'function' &&
  typeof Srf.prototype.recoverDialogs === 'function';

function redisUp() {
  return new Promise((resolve) => {
    const s = net.connect(REDIS_PORT, REDIS_HOST);
    s.setTimeout(800);
    s.on('connect', () => { s.destroy(); resolve(true); });
    s.on('error', () => resolve(false));
    s.on('timeout', () => { s.destroy(); resolve(false); });
  });
}

function flushHaKeys() {
  try {
    execSync(
      `redis-cli -h ${REDIS_HOST} -p ${REDIS_PORT} --scan --pattern 'ha:*' ` +
      `| xargs -r redis-cli -h ${REDIS_HOST} -p ${REDIS_PORT} del`,
      { stdio: 'ignore' });
  } catch (e) { /* best effort */ }
}

test('HA failover: server B recovers the dialog from redis and routes the BYE', async (t) => {
  if (!haSupported) {
    t.skip('requires drachtio-srf with reattach support (set HA_SRF_PATH to the feature/ha checkout)');
    return;
  }
  if (!(await redisUp())) {
    t.skip(`requires redis at ${REDIS_HOST}:${REDIS_PORT}`);
    return;
  }
  flushHaKeys();

  let app;
  try {
    // --- server A: writes dialog state to redis as the call is established ---
    await start('./drachtio.conf.xml', HA_ARGS);

    app = new HaApp();                  // modified srf: auto re-attaches on reconnect
    await app.connect();
    const answered = app.accept();
    const byeReceived = answered.then(() => app.waitForBye());

    // sipp establishes the call, holds it open ~9s, then sends a BYE
    const sipp = execCmd('sipp -sf ./uac-ha-failover.xml 127.0.0.1:5090 -m 1', { cwd: './scenarios' });

    await answered;
    t.pass('call established on server A');
    await delay(500);

    // --- crash A, promote B: start() stops A then starts B on the same ports ---
    await start('./drachtio.conf.xml', HA_ARGS.concat(['--recover-on-start']));
    t.pass('server B started with --recover-on-start');

    // app auto-reconnects to B and re-attaches; then sipp's BYE must reach the app via B
    const got = await byeReceived;
    t.ok(got, 'application received the BYE routed by the recovered server B');

    await sipp;
    t.pass('sipp received 200 OK to its BYE');
  } catch (err) {
    t.fail(`failed with error: ${err}`);
  } finally {
    if (app) app.disconnect();
    await stop().catch(() => {});
    flushHaKeys();
  }
});
