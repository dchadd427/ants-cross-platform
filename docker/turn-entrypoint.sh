#!/bin/sh
# The relay's entry point (Dockerfile.turn): it refuses to start unless its secret, its public address and its name are set (an empty secret would be a key that everybody knows), then hands
# over to the image's own entry point, which expands `$` in the arguments (the compose file gives them as `$ANTS_TURN_SECRET` and so on: the secret is not written into the command).
if [ -z "$ANTS_TURN_SECRET" ] || [ -z "$ANTS_TURN_EXTERNAL_IP" ] || [ -z "$ANTS_TURN_HOST" ]; then
    echo "ants-turn: set ANTS_TURN_SECRET, ANTS_TURN_EXTERNAL_IP and ANTS_TURN_HOST" >&2
    exit 1
fi
exec /usr/local/bin/docker-entrypoint.sh "$@"
