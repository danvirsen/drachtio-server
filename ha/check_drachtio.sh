#!/bin/sh
# Keepalived health check for drachtio-server.
# Exit 0 = healthy (process alive AND admin port accepting), non-zero = unhealthy.
#
# The SBC should ALSO health-check the floating VIP with SIP OPTIONS; this script is
# the node-local liveness probe Keepalived uses to decide MASTER/BACKUP.

PIDFILE="${DRACHTIO_PIDFILE:-/var/run/drachtio.pid}"
ADMIN_HOST="${DRACHTIO_ADMIN_HOST:-127.0.0.1}"
ADMIN_PORT="${DRACHTIO_ADMIN_PORT:-9022}"

# 1) process alive?
[ -f "$PIDFILE" ] || exit 1
pid=$(cat "$PIDFILE" 2>/dev/null)
[ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || exit 1

# 2) admin (control) port accepting connections?
if command -v nc >/dev/null 2>&1; then
    nc -z -w2 "$ADMIN_HOST" "$ADMIN_PORT" >/dev/null 2>&1 || exit 1
fi

exit 0
