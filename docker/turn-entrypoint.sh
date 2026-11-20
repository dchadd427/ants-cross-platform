#!/bin/sh
# The relay's entry point (Dockerfile.turn). It
#   - takes the listening port from ANTS_TURN_PORT (3478, TURN's own, when it is not set) and refuses a port that is not 1 - 65535: this is the one home of the number (the stack hands the same variable to the
#     game server, which tells the players, and to the health check, which is this script with --health);
#   - refuses to start unless the secret, the public address and the name are set (an empty secret would be a key that everybody knows), and refuses a name that carries a port: the port is ANTS_TURN_PORT only;
#   - then hands over to the image's own entry point, which expands `$` in the arguments (the compose file gives them as `$ANTS_TURN_SECRET` and so on: the secret is not written into the command).
port="${ANTS_TURN_PORT:-3478}"
case "$port" in
    ''|*[!0-9]*) echo "ants-turn: ANTS_TURN_PORT must be a port number, 1 - 65535" >&2; exit 1 ;;
esac
if [ "${#port}" -gt 5 ] || [ "$port" -lt 1 ] || [ "$port" -gt 65535 ]; then
    echo "ants-turn: ANTS_TURN_PORT must be a port number, 1 - 65535" >&2
    exit 1
fi
if [ "$1" = "--health" ]; then
    exec turnutils_stunclient -p "$port" 127.0.0.1
fi
if [ -z "$ANTS_TURN_SECRET" ] || [ -z "$ANTS_TURN_EXTERNAL_IP" ] || [ -z "$ANTS_TURN_HOST" ]; then
    echo "ants-turn: set ANTS_TURN_SECRET, ANTS_TURN_EXTERNAL_IP and ANTS_TURN_HOST" >&2
    exit 1
fi
case "$ANTS_TURN_HOST" in
    *:*) echo "ants-turn: ANTS_TURN_HOST is the name only; the port is ANTS_TURN_PORT" >&2; exit 1 ;;
esac
exec /usr/local/bin/docker-entrypoint.sh "$@" "--listening-port=$port"
