#!/bin/sh
# Runs a command (the web server) and keeps the log that it writes in the folder of its logs for 14 days, then deletes it. nginx writes its access log and the error log of its requests into one file there
# (docker/nginx.conf), and both name the visitor's address; the Legal page says that they are deleted after 14 days. Docker's own log of a container can be cut by size only (docs/BUILD_AND_RUN.md "The web server's
# logs"), never by age, so the image does it, the way a log is always rotated: every 10 minutes the live file is renamed to a file of its own and the command is told to open a new one (SIGUSR1: nginx reopens its log
# files), and the renamed files whose last line is more than 14 days old are deleted. A line is deleted between 14 days and 14 days and 25 minutes after it was written, never before; a container that is made new (a
# deploy) takes its files with it.
#
# The command writes to the files itself, with nothing between it and them (no pipe: a reader that is slow or gone would hold up the web server at its next line), and it is the command that this script becomes
# (exec: the process keeps this script's number, so that it is the container's first process and gets the signal that stops the container). What the cutting does can fail without any harm to it.
#
#   sh ants-web-log.sh COMMAND [ARGUMENT...]
#
# ANTS_LOG_DIR (/var/log/ants) and ANTS_LOG_TICK (600 seconds) are there for the tests only: the number of days is not a setting.
set -u
umask 077
KEEP_DAYS=14
DIR=${ANTS_LOG_DIR:-/var/log/ants}
TICK=${ANTS_LOG_TICK:-600}
live="$DIR/web.log"
command_pid=$$

# The folder can be entered (not read) by every user: the web server's workers run as a user of their own and open the new file by its name after a cut. The files are the web server's, and no one else's.
mkdir -p "$DIR" && chmod 711 "$DIR" || echo "web-log.sh: the folder $DIR cannot be made: the web server will say so" >&2

# The live file goes aside as a file of its own, which keeps the time of its last line as its own time: that is the time that prune reads. Two cuts in one second get two names. It is no cut when there is nothing to
# cut (the status says so: 1), and the command is told only after a real one.
cut_file() {
    [ -s "$live" ] || return 1
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

# At the start first: a container that was stopped for a while holds a live file with old lines in it, and it must not take the new ones into it. Nothing runs yet that could be told.
cut_file || :
prune || :

# Every tick: the cut, the command's new file (it opens the new file at once, and the lines that it was still writing to the old one are in the cut file, whose time is then the time of its last line), the deletion.
# Nothing here stops the command, and nothing in it waits for the loop: the loop is a process of its own, and a failure of any of its steps is the end of that step only.
(
    set +e
    while :; do
        sleep "$TICK"
        if cut_file; then
            kill -USR1 "$command_pid"
        fi
        prune
    done
) &
exec "$@"
