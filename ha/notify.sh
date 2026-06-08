#!/bin/sh
# Keepalived state-transition handler for drachtio-server.
#   master  -> become active: (re)start drachtio WITH --recover-on-start
#   backup  -> become passive: stop the local drachtio (only one active at a time)
#   fault   -> stop the local drachtio
#
# Adjust BIN/CONFIG/OPTS to your deployment. OPTS MUST include the --ha-* flags that
# point at the shared redis so recovery can find the dialogs.

BIN="${DRACHTIO_BIN:-/usr/local/bin/drachtio}"
CONFIG="${DRACHTIO_CONFIG:-/etc/drachtio.conf.xml}"
PIDFILE="${DRACHTIO_PIDFILE:-/var/run/drachtio.pid}"
OPTS="${DRACHTIO_OPTS:---ha-enabled --ha-redis-sentinels 10.0.0.10:26379,10.0.0.11:26379 --ha-redis-master mymaster}"

STATE="$1"

stop_drachtio() {
    if [ -f "$PIDFILE" ]; then
        pid=$(cat "$PIDFILE" 2>/dev/null)
        [ -n "$pid" ] && kill -TERM "$pid" 2>/dev/null
        # wait up to 20s for graceful exit
        i=0; while [ $i -lt 20 ] && kill -0 "$pid" 2>/dev/null; do sleep 1; i=$((i+1)); done
        kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null
        rm -f "$PIDFILE"
    fi
}

start_drachtio_with_recovery() {
    # make sure no stale instance is running
    stop_drachtio
    logger -t drachtio-ha "becoming MASTER: starting drachtio with --recover-on-start"
    PID=$($BIN -f "$CONFIG" $OPTS --recover-on-start --daemon)
    [ -n "$PID" ] && echo "$PID" > "$PIDFILE"
}

case "$STATE" in
    master) start_drachtio_with_recovery ;;
    backup) logger -t drachtio-ha "becoming BACKUP: stopping local drachtio"; stop_drachtio ;;
    fault)  logger -t drachtio-ha "FAULT: stopping local drachtio"; stop_drachtio ;;
    *)      echo "usage: $0 {master|backup|fault}"; exit 1 ;;
esac
exit 0
