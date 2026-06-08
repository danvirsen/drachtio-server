/*
 * Minimal srf application used by the HA reattach e2e (test/ha-reattach.js).
 *
 * Requires a drachtio-srf build that supports dialog persistence + reattach
 * (the feature/ha branch). Point at it with HA_SRF_PATH if it is not the
 * installed drachtio-srf, e.g. HA_SRF_PATH=../../drachtio-srf.
 */
const Srf = require(process.env.HA_SRF_PATH || 'drachtio-srf');
const config = require('./config');

class HaApp {
  constructor(store) {
    this.srf = new Srf();
    this.srf.on('error', () => { /* swallow in tests */ });
    if (store && typeof this.srf.enableDialogPersistence === 'function') {
      this.srf.enableDialogPersistence(store);
    }
  }

  connect() {
    this.srf.connect(config.drachtio.connectOpts);
    return new Promise((resolve, reject) => {
      this.srf.on('connect', (err) => (err ? reject(err) : resolve()));
    });
  }

  // answer the next incoming INVITE and remember the resulting dialog
  accept() {
    return new Promise((resolve, reject) => {
      this.srf.invite((req, res) => {
        const localSdp = req.body.replace(/m=audio\s+(\d+)/, 'm=audio 15000');
        this.srf.createUAS(req, res, { localSdp })
          .then((uas) => {
            this.dlg = uas;
            uas.metadata = { test: 'ha-reattach' }; // business state that should survive the restart
            resolve(uas);
          })
          .catch(reject);
      });
    });
  }

  // rebuild dialogs from the shared store and reattach them to the server
  async recover() {
    const dialogs = await this.srf.recoverDialogs();
    this.dlg = dialogs[0];
    return dialogs;
  }

  // originate an in-dialog BYE on the (recovered) dialog
  bye() {
    return this.dlg.destroy();
  }

  // resolve(true) when the network sends a BYE for our dialog (dialog 'destroy')
  waitForBye() {
    return new Promise((resolve) => {
      if (!this.dlg) return resolve(false);
      this.dlg.on('destroy', () => resolve(true));
    });
  }

  disconnect() {
    try { this.srf.disconnect(); } catch (e) { /* already gone */ }
    return this;
  }
}

module.exports = HaApp;
