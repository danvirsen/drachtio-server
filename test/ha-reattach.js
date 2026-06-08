/*
 * End-to-end test for the high-availability `reattach` verb and recovered-dialog routing.
 *
 * Scenario (application-restart recovery, approach B — needs NO redis and NO server HA flags,
 * because only the application restarts while the locally-built drachtio keeps the dialog):
 *
 *   1. App A answers an INVITE from sipp and persists the dialog to a shared store.
 *   2. App A "crashes" (disconnects). drachtio keeps the SIP dialog + leg.
 *   3. App B (sharing the store) reconnects, recoverDialogs() rebuilds the dialog and
 *      sends `reattach` so the server re-binds dialogId -> B's socket.
 *   4. B's recovered dialog originates an in-dialog BYE; the server routes it to sipp,
 *      which is waiting to receive exactly that BYE. sipp exiting 0 proves the round-trip.
 *
 * Requires a drachtio-srf with reattach/recoverDialogs (the feature/ha branch). If the
 * resolved drachtio-srf lacks it, the test skips. Point at the sibling checkout with:
 *   HA_SRF_PATH=../../drachtio-srf node ha-reattach.js
 *
 * Requires the locally-built ../build/drachtio (which carries the reattach verb) and sipp,
 * same as the rest of this e2e suite.
 */
const test = require('blue-tape');
const { start, stop } = require('./testbed');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');

let Srf;
try { Srf = require(process.env.HA_SRF_PATH || 'drachtio-srf'); } catch (e) { /* not installed */ }
const HaApp = require('./scripts/ha-app');

const haSupported = !!Srf &&
  typeof Srf.prototype.recoverDialogs === 'function' &&
  typeof Srf.prototype.enableDialogPersistence === 'function';

function memStore() {
  const m = new Map();
  return {
    async set(id, rec) { m.set(id, rec); },
    async del(id) { m.delete(id); },
    async list() { return [...m.values()]; }
  };
}

test('HA: recovered dialog reattaches and routes an in-dialog BYE', async (t) => {
  if (!haSupported) {
    t.skip('requires drachtio-srf with reattach/recoverDialogs (set HA_SRF_PATH to the feature/ha checkout)');
    return;
  }

  const store = memStore();
  let appA, appB;
  try {
    await start();                      // local ../build/drachtio (has the reattach verb)

    appA = new HaApp(store);
    await appA.connect();
    const answered = appA.accept();     // arm INVITE handler

    // sipp sends INVITE, ACKs the 200, then waits to receive a BYE
    const sipp = execCmd('sipp -sf ./uac-recv-bye.xml 127.0.0.1:5090 -m 1', { cwd: './scenarios' });

    const dlgA = await answered;
    t.ok(dlgA.id, `call established (dialog id ${dlgA.id})`);
    await delay(250);                   // let the dialog record flush to the store

    // --- simulate application restart ---
    appA.disconnect();                  // drachtio keeps the dialog; the app socket is gone
    await delay(300);

    appB = new HaApp(store);            // fresh process would read the same external store
    await appB.connect();
    const recovered = await appB.recover();

    t.equal(recovered.length, 1, 'app B recovered one dialog from the store');
    t.equal(recovered[0].id, dlgA.id, 'recovered dialog id matches the original');
    t.deepEqual(recovered[0].metadata, { test: 'ha-reattach' }, 'business metadata survived the restart');

    // the recovered dialog is reattached on the server; originate an in-dialog BYE through it
    await appB.bye();

    // sipp must have received that BYE and exited cleanly
    await sipp;
    t.pass('sipp received the BYE on the reattached dialog');
  } catch (err) {
    t.fail(`failed with error: ${err}`);
  } finally {
    if (appA) appA.disconnect();
    if (appB) appB.disconnect();
    await stop().catch(() => {});
  }
});
