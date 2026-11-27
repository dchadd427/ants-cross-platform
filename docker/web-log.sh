#!/bin/sh
# Runs a command (the web server) and keeps what it writes to its output and its error output for 14 days, then deletes it. nginx writes its access log to the one and its error log to the other, and both name
# the visitor's address; the Legal page says that they are deleted after 14 days. Docker's own log of a container can be cut by size only (docs/BUILD_AND_RUN.md "The web server's logs"), never by age, so the
# image does it: the output goes into a pipe, a small loop appends it to one file, and a second loop cuts that file into a new one every 10 minutes and deletes the cut files whose last line is more than 14
# days old. A line is deleted between 14 days and 14 days and 25 minutes after it was written, never before; a container that is made new (a deploy) takes its files with it.
#
#   sh ants-web-log.sh COMMAND [ARGUMENT...]
#
# ANTS_LOG_DIR (/var/log/ants) and ANTS_LOG_TICK (600 seconds) are there for the tests only: the number of days is not a setting.
set -eu
umask 077
KEEP_DAYS=14
DIR=${ANTS_LOG_DIR:-/var/log/ants}
TICK=${ANTS_LOG_TICK:-600}
live="$DIR/web.log"
pipe="$DIR/.pipe"

mkdir -p "$DIR"

# The live file goes aside as a file of its own, which keeps the time of its last line as its own time: that is the time that prune reads. Two cuts in one second get two names.
cut_file() {
    [ -s "$live" ] || return 0
    stamp=$(date -u +%Y%m%dT%H%M%SZ)
    name="$DIR/web.$stamp.log"
    n=0
    while [ -e "$name" ]; do
        n=$((n + 1))
        name="$DIR/web.$stamp.$n.log"
    done
    mv "$live" "$name"
}

prune() {
    find "$DIR" -maxdepth 1 -type f -name 'web.*.log' -mmin +$((KEEP_DAYS * 24 * 60)) -exec rm -f {} +
}

# At the start first: a container that was stopped for a while holds a live file with old lines in it, and it must not take the new ones into it.
cut_file
prune

[ -p "$pipe" ] || mkfifo "$pipe"
( while :; do sleep "$TICK"; cut_file || :; prune || :; done ) &
# (the file is opened for every line: a cut is a rename, and the next line makes the new live file. The reader never stops for a failed write, so the web server is never held up by it.)
( while IFS= read -r line || [ -n "$line" ]; do printf '%s\n' "$line" >> "$live" || :; done < "$pipe" ) &
exec "$@" > "$pipe" 2>&1
