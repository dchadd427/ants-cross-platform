#!/usr/bin/env bash
# Calls the deploy webhook that the environment variable WEBHOOK_URL holds (a Portainer stack's webhook; the deploy job of .github/workflows/ci.yml takes it from a repository
# secret). Tested by tests/scripts/test_deploy_webhook.py.
#
#   WEBHOOK_URL=<address> tools/deploy_webhook.sh [LABEL]       LABEL (production, staging) only names the site in the messages
#
# The address is secret: it is read from the environment (never from an argument), handed to curl through its standard input (never on a command line, where `ps` shows
# it), and never printed: curl's own messages are switched off because they name the host, and the messages here say the status and nothing else. A POST with a timeout
# and a few retries (a few seconds apart; DEPLOY_RETRY_DELAY changes that). Without an address: "deploy secret not set: skipped", and success, so that nothing changes
# until the secret is set. Exit status: 0 called (or skipped), 1 the call failed.
label="${1:-the site}"
if [ -z "${WEBHOOK_URL:-}" ]; then
    echo "deploy secret not set: skipped"
    exit 0
fi
case "$WEBHOOK_URL" in
    http://*|https://*) ;;
    *) echo "deploy webhook ($label): the secret is not an http(s) address: not called"; exit 1 ;;
esac
case "$WEBHOOK_URL" in
    *[[:space:]\"\\]*) echo "deploy webhook ($label): the secret holds a blank, a quote or a backslash: not called"; exit 1 ;;
esac
code=$(printf 'url = "%s"\n' "$WEBHOOK_URL" | curl --config - --silent --output /dev/null --write-out '%{http_code}' --request POST \
    --connect-timeout 10 --max-time 30 --retry 3 --retry-delay "${DEPLOY_RETRY_DELAY:-5}" --retry-all-errors --fail)
status=$?
if [ "$status" -ne 0 ]; then
    echo "deploy webhook ($label): FAILED (curl exit status $status, HTTP $code)"
    exit 1
fi
echo "deploy webhook ($label): called, HTTP $code"
exit 0
