const test = require('tape');
const Srf = require('drachtio-srf');
const config = require('./scripts/config');
const execCmd = require('./utils/exec');
const {start, stop, waitForPortInUse} = require('./testbed');

// When the app handling a ringing call disconnects, drachtio refuses the call to the caller and cancels it
// towards the callee, instead of leaving both ringing until they give up.

const connect = () => {
  const srf = new Srf();
  srf.connect(config.drachtio.connectOpts);
  return new Promise((resolve, reject) => {
    srf.on('connect', (err) => err ? reject(err) : resolve(srf));
    srf.on('error', () => {});
  });
};

test('a ringing incoming call is answered 480 when its app disconnects', async(t) => {
  let srf;
  try {
    await start(null, []);
    srf = await connect();
    srf.invite((req, res) => {
      res.send(180);
      setTimeout(() => srf.disconnect(), 500);
    });
    await execCmd('sipp -sf ./uac-ring-480.xml 127.0.0.1:5090 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    t.pass('the caller gets 480 once the app has gone');
  } catch (err) {
    t.fail(`failed with error ${err}`);
    if (srf) srf.disconnect();
  }
  await stop();
  t.end();
});

test('a ringing outgoing INVITE is cancelled when its app disconnects', async(t) => {
  let srf;
  try {
    await start(null, []);
    srf = await connect();
    const callee = execCmd('sipp -sf ./uas-cancel.xml -i 127.0.0.1 -p 5091 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    await waitForPortInUse(5091);
    srf.createUAC('sip:127.0.0.1:5091', {localSdp: 'v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 9 RTP/AVP 0\r\n'}, {
      cbProvisional: () => srf.disconnect()
    }).catch(() => {});
    await callee;
    t.pass('the callee gets a CANCEL once the app has gone');
  } catch (err) {
    t.fail(`failed with error ${err}`);
    if (srf) srf.disconnect();
  }
  await stop();
  t.end();
});

test('an answer that crosses the CANCEL of a departed app\'s INVITE is ACKed and hung up', async(t) => {
  let srf;
  try {
    await start(null, []);
    srf = await connect();
    const callee = execCmd('sipp -sf ./uas-200-crossing-cancel.xml -i 127.0.0.1 -p 5091 -m 1 -timeout 10s -timeout_error', {cwd: './scenarios'});
    await waitForPortInUse(5091);
    srf.createUAC('sip:127.0.0.1:5091', {localSdp: 'v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\nm=audio 9 RTP/AVP 0\r\n'}, {
      cbProvisional: () => srf.disconnect()
    }).catch(() => {});
    await callee;
    t.pass('the callee gets an ACK and a BYE for its answer');
  } catch (err) {
    t.fail(`failed with error ${err}`);
    if (srf) srf.disconnect();
  }
  await stop();
  t.end();
});
