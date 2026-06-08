# High Availability Setup Guide

This guide explains how to run drachtio-server in an active/passive high‑availability cluster and show how to recover SIP dialogs and perform a failover to the passive node.  
It covers the redis-backed dialog store, the configuration flags, both Keepalived and Pacemaker deployment paths, the required `drachtio-srf` behavior, and the failover acceptance test.

---

## 1. How it works

A live call in drachtio is held together by three pieces of state. HA persists and recovers all three:

| State                                                            | Normally                   | Under HA                                                                   |
| ---------------------------------------------------------------- | -------------------------- | -------------------------------------------------------------------------- |
| **SIP dialog** (call-id, tags, routes, SDP, CSeq, session timer) | in-memory only             | written through to **redis** on every change                               |
| **Sofia leg** (routes in-dialog requests)                        | instance-local pointer     | **recreated** from redis on the recovering node (`nta_leg_tcreate`)        |
| **App connection** (the socket to your srf app)                  | one socket on one instance | app **reconnects and re-attaches** its dialogs after failover (`reattach`) |

Active/passive: exactly **one** drachtio instance is active at a time, behind a floating **virtual IP**. When the active node dies, the VIP moves to the standby, which starts with `--recover-on-start`. It loads every dialog from redis, recreates the Sofia legs, restarts session timers, and is ready before the first in-dialog request arrives. Your srf app reconnects to the VIP and re-attaches its dialogs. A re-INVITE or BYE that lands on the new node is matched to the recovered leg and routed to the re-attached app.

```
      ┌───────────────────────┐
      │  SBC / load balancer  │
      └───────────┬───────────┘
                :5060
          ┌───────┴────────┐
          │  Floating VIP  │       ┌───────────┐
          │   10.0.0.100   ├─:9022─┤  SRF app  │
          └───────┬────────┘       └───────────┘
                  │
        ╭─────────╯
 ┌──────┴───────┐   ┌──────────────┐
 │  drachtio-1  │   │  drachtio-2  │
 │   (active)   │   │  (passive)   │
 └─────────┬────┘   └────┬─────────┘
           │ ┌─────────┐ │
           ╰─┤  Redis  ├─╯
             └─────────┘
```

---

## 2. Requirements

- A **shared redis** reachable by all drachtio nodes.
- A **virtual IP** manager like **Keepalived** or **Pacemaker/Corosync**.
- **Fencing/STONITH** (Pacemaker) or correct VRRP priorities (Keepalived) so two nodes never both own the VIP (split brain).
- **drachtio-srf** built with reconnect + reattach support.
- **Inbound application connections** (`srf.connect()`). HA failover is **not** supported with outbound connections (`srf.listen()`).

---

## 3. Configuration

HA is **off** by default. Enable it with `--ha-enabled` plus redis coordinates. Every option is available as a CLI flag and an environment variable. HA requires its own redis configuration (`--ha-redis-*`), but it can share the same instance as the blacklist.

### CLI flags

| Flag                             | Meaning                                                                                                         |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `--ha-enabled`                   | Turn on dialog replication.                                                                                     |
| `--recover-on-start`             | On startup, bulk-load + recover all dialogs from redis before serving SIP. Use this on the node being promoted. |
| `--ha-instance-id <id>`          | Unique id for this node (default `{hostname}-{pid}`). Used for redis ownership.                                 |
| `--ha-redis-address <host>`      | redis host (direct mode).                                                                                       |
| `--ha-redis-port <port>`         | redis port (default 6379).                                                                                      |
| `--ha-redis-password <pw>`       | redis AUTH password.                                                                                            |
| `--ha-redis-sentinels <h:p,h:p>` | redis Sentinel list (sentinel mode).                                                                            |
| `--ha-redis-master <name>`       | redis Sentinel master name.                                                                                     |
| `--ha-ownership-ttl <secs>`      | Ownership key TTL (default 30).                                                                                 |

### Environment variables

| Variable                      | Equivalent             |
| ----------------------------- | ---------------------- |
| `DRACHTIO_HA_ENABLED=1`       | `--ha-enabled`         |
| `DRACHTIO_RECOVER_ON_START=1` | `--recover-on-start`   |
| `DRACHTIO_HA_INSTANCE_ID`     | `--ha-instance-id`     |
| `DRACHTIO_HA_REDIS_ADDRESS`   | `--ha-redis-address`   |
| `DRACHTIO_HA_REDIS_PORT`      | `--ha-redis-port`      |
| `DRACHTIO_HA_REDIS_PASSWORD`  | `--ha-redis-password`  |
| `DRACHTIO_HA_REDIS_SENTINELS` | `--ha-redis-sentinels` |
| `DRACHTIO_HA_REDIS_MASTER`    | `--ha-redis-master`    |
| `DRACHTIO_HA_OWNERSHIP_TTL`   | `--ha-ownership-ttl`   |

### Examples

Direct redis (testing):

```bash
drachtio -f /etc/drachtio.conf.xml \
  --ha-enabled \
  --ha-redis-address 10.0.0.10 --ha-redis-port 6379
```

redis Sentinel (production):

```bash
drachtio -f /etc/drachtio.conf.xml \
  --ha-enabled \
  --ha-redis-sentinels 10.0.0.10:26379,10.0.0.11:26379,10.0.0.12:26379 \
  --ha-redis-master mymaster \
  --ha-redis-password "$REDIS_PW"
```

The node being **promoted** additionally gets `--recover-on-start` (the OCF agent and the Keepalived notify script add it for you).

---

## 4. Deployment examples

### Keepalived

Files are provided under `ha/`:

- `ha/keepalived.conf.sample` — VRRP instance owning the VIP.
- `ha/check_drachtio.sh` — node-local liveness probe.
- `ha/notify.sh` — starts drachtio **with recovery** on MASTER, stops it on BACKUP.

#### Setup

1. Install redis (Sentinel for prod) reachable from both nodes.
2. Copy `ha/notify.sh` and `ha/check_drachtio.sh` to `/etc/keepalived/` on both nodes (`chmod +x`). Edit `DRACHTIO_OPTS` in `notify.sh` to your redis coordinates.
3. Install `ha/keepalived.conf.sample` as `/etc/keepalived/keepalived.conf` on both nodes. On node 1 keep `state MASTER` / `priority 150`; on node 2 set `state BACKUP` / `priority 100`. Set the real `interface`, `virtual_ipaddress`, and `auth_pass`.
4. **Do not** also start drachtio from systemd — Keepalived's `notify.sh` owns its lifecycle. Disable the systemd unit (`systemctl disable --now drachtio`).
5. Start Keepalived on both nodes. The MASTER takes the VIP and starts drachtio.
6. Point the SBC / load balancer at the VIP (SIP, port 5060) and health-check it with **SIP OPTIONS**.
7. Point the **application** at the VIP on the admin/control port (`9022`) via `srf.connect({host: '<VIP>', port: 9022, ...})`.

#### Perform a failover

Kill drachtio (or the whole node) on the MASTER → its priority drops / VRRP fails over → node 2 takes the VIP → `notify.sh master` starts drachtio with `--recover-on-start` → dialogs recovered → calls continue.

### Pacemaker

Use the OCF resource agent `ha/ocf-drachtio`:

1. Install it as `/usr/lib/ocf/resource.d/drachtio/drachtio` (`chmod +x`).
2. Define a VIP resource (`ocf:heartbeat:IPaddr2`) and the drachtio primitive, colocated and ordered after the VIP:

   ```bash
   primitive vip_drachtio ocf:heartbeat:IPaddr2 \
      params ip="10.0.0.100" cidr_netmask="24" nic="eth0" \
      op monitor interval="2s"

   primitive p_drachtio ocf:drachtio:drachtio \
      params binary="/usr/local/bin/drachtio" \
            config="/etc/drachtio.conf.xml" \
            opts="--ha-enabled --ha-redis-sentinels 10.0.0.10:26379,10.0.0.11:26379 --ha-redis-master mymaster" \
            pidfile="/var/run/drachtio.pid" \
      op monitor interval="5s" timeout="20s" \
      op start   timeout="60s" \
      op stop    timeout="30s"

   colocation c_drachtio_on_vip inf: p_drachtio vip_drachtio
   order o_vip_then_drachtio inf: vip_drachtio p_drachtio
   ```

The agent always starts drachtio with `--recover-on-start`. Recovery is a safe no-op when redis is empty (cold start). The PID file is written **after** recovery completes, so Pacemaker's start action blocks until the node is genuinely ready.

3. Configure **STONITH/fencing** so a partitioned old-active is killed before the standby promotes. Ownership TTL in redis is a secondary guard, not a substitute for fencing.

## 5. drachtio-srf application support

The server side persists and recovers the SIP/Sofia state, but the **application socket cannot move between instances**. The app must survive the failover and re-attach its dialogs. This is handled by `drachtio-srf` via a `reattach` control message.

### Connecting the application

The application opens the control connection to drachtio (inbound mode), so it must connect to the **floating VIP** (not to any node's real IP) on the admin/control port (default `9022`). The same VIP carries both planes, on different ports:

| Plane                    | Direction      | Target | Port (default) |
| ------------------------ | -------------- | ------ | -------------- |
| SIP                      | SBC → drachtio | VIP    | 5060           |
| Control (drachtio ↔ srf) | app → drachtio | VIP    | 9022           |

```js
const srf = new Srf();
srf.connect({
  host: "10.0.0.100", // the floating VIP, NOT node1/node2's real address
  port: 9022, // drachtio admin/control port
  secret: "cymru",
});
```

Why the VIP: `srf.connect()` always reconnects to the same `host:port` it was given. That address must follow the active node. If the app pointed at a node's real IP, after a failover it would keep retrying the dead node and never reach the survivor. drachtio binds the admin port on `0.0.0.0`, so whichever node currently holds the VIP answers on `VIP:9022`. (A separate floating management VIP also works, as long as it moves with the active node.)

> **Co-located apps:** if the srf app runs on the same host as drachtio, it must still connect
> to the **VIP**, not `127.0.0.1:9022`. A localhost control connection breaks HA, because after
> a failover the local drachtio is gone.

### Automatic (inbound / `srf.connect()` mode)

With a `drachtio-srf` build that includes reattach support, **no application code change is required** when you connect with `srf.connect(...)`:

1. `Dialog` objects are kept alive across a server disconnect (they are not torn down when the socket drops).
2. The underlying connection auto-reconnects to the VIP (now the new active node) and re-authenticates.
3. On reconnect, srf repoints every live dialog at the new socket and sends `reattach|<dialogId,...>`. Because the promoted node may still be loading dialogs from redis, srf retries briefly until all are accepted.

Wire format (handled for you):

```
<len>#<msgId>|reattach|<dialogId1,dialogId2,...>
```

The server verifies each dialog exists in the recovered set and binds it to the socket, rebuilding the `dialogId → client` routing map. Response: `OK|reattached N of M`.

Optional events on the `Srf` instance:

```js
srf.on("reattached", ({ count, response }) => {
  /* dialogs re-bound after failover */
});
srf.on("reattach-failed", (err) => {
  /* could not re-attach; calls may be orphaned */
});
```

### Application restart

Server failover (above) assumes the **application** stays up across the event, i.e. it still has its `Dialog` objects and reconnects after the failover is complete. If instead the **application process itself restarts**, its in-memory dialogs and business state are lost, and reattach alone cannot recover them.

To survive an application restart, the app needs to persist its own dialog records (SIP plumbing plus opaque business `metadata`) to a store and rebuild them on startup with `srf.enableDialogPersistence()` / `srf.recoverDialogs()`.

### Outbound connections / `srf.listen()` mode -- NOT SUPPORTED

> **HA failover does not work with outbound connections today.**  
> Use inbound (`srf.connect()`) connections if you need HA.

With outbound connections the **server** dials the application (per call, via `makeOutboundConnection`, keyed by the initial INVITE's transaction id). That inverts every assumption the reattach flow depends on, and none of the pieces are wired:

- **No socket is re-established on failover.** Outbound connections are created only by route logic on an _initial INVITE_. `recoverDialogsFromRedis()` rebuilds the SIP dialog and Sofia leg but never calls `makeOutboundConnection`, and there is no new INVITE to trigger one, so a recovered dialog has a leg but no application socket.
- **The srf reconnect/reattach path is client-mode only.** In `listen()` mode the app is a passive server: it does not dial out, does not auto-reconnect, and never fires the reattach logic. (The `reconnected` event is emitted only for client-mode connections.)
- **The outbound target is not persisted.** `DialogState` records the app name, not the host:port/transport needed to dial the app back.
- **Direction/keying mismatch.** `reattach` is sent app→server and outbound binding is keyed by the original INVITE transaction id, which is gone after failover.

The result: after a failover, recovered dialogs on the new node have a leg but no app socket, and in-dialog requests cannot be delivered to the application. See §12 for the work required to support this.

## 6. Session timers under HA

RFC 4028 session timers are handled so that, with multiple instances involved:

- **Refreshes are never sent from two instances at once.** Only the instance that currently **owns** the dialog in redis arms the "send refreshing re-INVITE" timer. A non-owner keeps the refresher role but does not send.
- **A refresh received on any instance keeps the call alive everywhere.** Each received re-INVITE/UPDATE writes `lastRefreshTs` into the dialog's redis hash. Before a "peer did not refresh" teardown timer kills a call, it re-reads `lastRefreshTs`; if the peer refreshed within the interval (on any node), it reschedules instead of tearing down.

No configuration is required; this is automatic when `--ha-enabled`.

## 7. Acceptance test

The core test: a call started on node 1 survives node 1 dying and is then controlled through node 2.

1. Bring up redis + two drachtio nodes + the VIP (Keepalived or Pacemaker). Node 1 active.
2. Place a call through the VIP from your srf app. Confirm state in redis:

   ```bash
   redis-cli SMEMBERS ha:dialogs
   redis-cli HGETALL ha:dialog:<dialogId>
   redis-cli GET ha:owner:<dialogId>          # -> node 1 instance id
   ```

3. **Kill node 1** (`kill -9` the process, or power off the VM).
4. The VIP moves to node 2; node 2 starts/recovers (`--recover-on-start`). Confirm:

   ```bash
   # node 2 log: "recovered N of M dialog(s) from redis"
   redis-cli GET ha:owner:<dialogId>          # -> node 2 instance id
   ```

5. The srf app's connection to node 1 drops; it reconnects to the VIP (node 2) and sends
   `reattach` with the dialog id.
6. From the far end, send a **re-INVITE** or **BYE** to the VIP. It lands on node 2, matches
   the recovered leg, and is delivered to the re-attached app. **The call does not break.**
7. Session-timer variant: after failover, let the far end send a session refresh to node 2;
   confirm no duplicate refresher and no spurious teardown.

### Automated tests

- **Unit**  
  `make check` builds and runs `test_dialog_state`, a `DialogState` serialization round-trip (the data that recovery depends on). No redis or Sofia needed.
- **End-to-end, application restart**  
  `test/ha-reattach.js`: an application answers a call and persists the dialog, "restarts" (a second app instance recovers from the shared store and reattaches), then sends an in-dialog BYE the server routes to the reattached socket. Needs no redis or server HA flags (only the app restarts).

  ```bash
  cd test
  HA_SRF_PATH=../../drachtio-srf npm run test-ha
  ```

- **End-to-end, server failover**  
  `test/ha-failover.js`: drachtio A (`--ha-enabled`) writes the dialog to redis while a call is set up; A is killed and B is started with `--recover-on-start`; the (still-running) application auto-reconnects to B and re-attaches; sipp then sends a BYE that B matches against the **recovered Sofia leg** and routes to the application.

  ```bash
  cd test
  HA_SRF_PATH=../../drachtio-srf npm run test-ha-failover
  ```

Both e2e tests require the locally-built `../build/drachtio` and a drachtio-srf with reattach support (the `feature/ha` branch); `ha-failover.js` also needs a reachable redis (`HA_REDIS_HOST`/`HA_REDIS_PORT`, default `127.0.0.1:6379`). They skip automatically when their prerequisites are absent.

| Key                    | Type         | Contents                                                                                           |
| ---------------------- | ------------ | -------------------------------------------------------------------------------------------------- |
| `ha:dialogs`           | SET          | all active dialog ids (recovery enumeration)                                                       |
| `ha:dialog:{dialogId}` | HASH         | serialized `DialogState` (tags, URIs, SDP, transport, session timer, `lastRefreshTs`, app name, …) |
| `ha:owner:{dialogId}`  | STRING (TTL) | instance id of the current owner                                                                   |

Inspect / clean up:

```bash
redis-cli --scan --pattern 'ha:dialog:*'
redis-cli DEL ha:dialogs               # wipe the index (testing only)
```

## 8. Troubleshooting

| Symptom                                                     | Likely cause                                                                                                              |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `HA enabled but no redis configured; disabling HA` in log   | No `--ha-redis-*` values.                                                                                                 |
| Recovery logs `recovered 0 of 0` after failover             | redis empty, or dialogs were already removed (clean hangups), or another instance still owns them (TTL not yet expired).  |
| In-dialog request after failover gets 481 / no app response | srf app did not `reattach`, or the leg was not recreated (check the recovery log for `rebuilt leg`).                      |
| Call torn down at session-timer expiry after failover       | refresh landed on a node without ownership and `lastRefreshTs` not being written. Verify `--ha-enabled` on **all** nodes. |
| Both nodes active (double BYE, glare)                       | Split brain. Fix VRRP priorities / enable STONITH fencing.                                                                |

## 9. Limitations

- **Inbound connections only.** HA failover requires applications to use inbound connections (`srf.connect()`). Outbound connections (`srf.listen()`) are not supported for HA.
- drachtio is signaling-only. If your app fronts a media engine (rtpengine, FreeSWITCH), that media state must be made HA separately.
- A transaction in flight at the exact instant the active node dies may need retransmission from the far end after recovery.
- This is stricly active/passive. True active/active behind a round-robin load balancer additionally requires inter-instance request forwarding; it is not enabled here.

## 10. Future work: outbound-connection HA

Supporting failover with outbound connections (`srf.listen()`) requires, at minimum:

- Persist each dialog's outbound application target (host:port + transport) in `DialogState` alongside the app name.
- During `recoverDialogsFromRedis()`, re-establish an outbound connection to that target for each recovered dialog (reusing one socket where several dialogs share an endpoint).
- A **server-initiated** reattach handshake that binds the freshly-made outbound socket to the recovered dialog id (the reverse of today's app→server `reattach` verb).

Until that exists, run HA with inbound connections.
