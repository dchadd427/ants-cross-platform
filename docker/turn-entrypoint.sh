#!/bin/sh
# The relay's entry point (Dockerfile.turn). It
#   - takes the listening port from ANTS_TURN_PORT (3478, TURN's own, when it is not set) and refuses a port that is not 1 - 65535: this is the one home of the number (the stack hands the same variable to the
#     game server, which tells the players, and to the health check, which is this script with --health);
#   - refuses to start unless the secret, the public address and the name are set (an empty secret would be a key that everybody knows), and refuses a name that carries a port: the port is ANTS_TURN_PORT only;
#   - refuses an address that is not an IPv4 address of this machine (host networking: its own interfaces), because the relay listens on that address and sends from it, and on no other interface: with an address
#     that is not the machine's own, coturn would run and answer nobody;
#   - then hands over to the image's own entry point, which expands `$` in the arguments (the compose file gives them as `$ANTS_TURN_SECRET` and so on: the secret is not written into the command).
# The list of the machine's addresses is read from the kernel; the test (tests/scripts/test_turn_relay.py) points that path at a file of its own.
port="${ANTS_TURN_PORT:-3478}"
case "$port" in
    ''|*[!0-9]*) echo "ants-turn: ANTS_TURN_PORT must be a port number, 1 - 65535" >&2; exit 1 ;;
esac
if [ "${#port}" -gt 5 ] || [ "$port" -lt 1 ] || [ "$port" -gt 65535 ]; then
    echo "ants-turn: ANTS_TURN_PORT must be a port number, 1 - 65535" >&2
    exit 1
fi
# `ants_turn_address ADDRESS`: success only for an IPv4 address (digits and dots, nothing a shell or a pattern could read otherwise) that is one of this machine's own addresses and not a loopback address, where no
# player reaches the relay. The address is looked for as the kernel writes it, so a number with a leading zero, a part above 255 or a prefix of an address of the machine is not one.
ants_turn_address() {
    case "$1" in
        ''|*[!0-9.]*|127.*) return 1 ;;
    esac
    escaped=$(printf '%s' "$1" | sed 's/\./\\./g')
    grep -E -A1 -- "\\|-- ${escaped}\$" /proc/net/fib_trie 2>/dev/null | grep -q '/32 host LOCAL'
}
if [ "$1" = "--health" ]; then
    # the relay listens on its public address only: that is where the STUN binding is asked for
    ants_turn_address "$ANTS_TURN_EXTERNAL_IP" || { echo "ants-turn: ANTS_TURN_EXTERNAL_IP is not an address of this machine" >&2; exit 1; }
    exec turnutils_stunclient -p "$port" "$ANTS_TURN_EXTERNAL_IP"
fi
if [ -z "$ANTS_TURN_SECRET" ] || [ -z "$ANTS_TURN_EXTERNAL_IP" ] || [ -z "$ANTS_TURN_HOST" ]; then
    echo "ants-turn: set ANTS_TURN_SECRET, ANTS_TURN_EXTERNAL_IP and ANTS_TURN_HOST" >&2
    exit 1
fi
case "$ANTS_TURN_HOST" in
    *:*) echo "ants-turn: ANTS_TURN_HOST is the name only; the port is ANTS_TURN_PORT" >&2; exit 1 ;;
esac
ants_turn_address "$ANTS_TURN_EXTERNAL_IP" || { echo "ants-turn: ANTS_TURN_EXTERNAL_IP must be an IPv4 address of this machine (the relay listens on it and on no other address)" >&2; exit 1; }
exec /usr/local/bin/docker-entrypoint.sh "$@" "--listening-port=$port" "--listening-ip=$ANTS_TURN_EXTERNAL_IP" "--relay-ip=$ANTS_TURN_EXTERNAL_IP"
