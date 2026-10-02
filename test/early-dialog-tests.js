const test = require('tape');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const delay = require('./utils/delay');
const {start, stop} = require('./testbed');

// A request the caller sends on a ringing call (early dialog) must reach the app routing its method,
// and that app's answer must reach the caller.

const connect = () => {
  const srf = new Srf();
  srf.connect(config.drachtio.connectOpts);
  return new Promise((resolve, reject) => {
    srf.on('connect', (err) => err ? reject(err) : resolve(srf));
    srf.on('error', () => {});
  });
};

test('INFO on a ringing call is answered by the app routing INFO', async(t) => {
  let srf;
  try {
    await start(null, []);
    srf = await connect();
    let infoAnswered;
    const info = new Promise((resolve) => infoAnswered = resolve);
    srf.info((req, res) => res.send(200, (err) => infoAnswered(err || null)));
    srf.invite(async(req, res) => {
      res.send(180);
      await Promise.race([info, delay(5000)]);
      res.send(486);
    });
    await execCmd('sipp -sf ./uac-early-info-answered.xml 127.0.0.1:5090 -m 1 -timeout 15s -timeout_error',
      {cwd: './scenarios'});
    t.pass('the caller gets the app\'s 200 for its INFO, then 486 for the INVITE');
    t.equal(await Promise.race([info, delay(1000).then(() => 'none')]), null, 'drachtio accepts the app\'s answer');
  } catch (err) {
    t.fail(`failed with error ${err}`);
  }
  if (srf) srf.disconnect();
  await stop();
  t.end();
});
