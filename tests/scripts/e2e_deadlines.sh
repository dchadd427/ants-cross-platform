# Deadlines for the waits of tests/scripts/test_ants_server.sh (sourced by it; tests/scripts/test_e2e_deadlines.py tests every function).
#
# A program that does not end when it is told to (a server that does not stop on SIGTERM, a game that was stopped and is asked to end, a checker that never answers) used to hang the script, and with it the CI step,
# until the job was cancelled half an hour later, with no log of where. Every wait for a process has a deadline now: when it runs out the part FAILS with a line that names what it waited for, and the process is
# killed. The waits for an answer are loops of at most N turns of curl -m or of a socket with a timeout, and the part as a whole has a watchdog (start_watchdog) for a wait that nobody thought of.
#
# The caller has PART_LABEL (" [rooms]" or empty), FAILS and CHECKS (its counters of checks), calls stop_watchdog at its end, and adds hang_count to its failures. Plain bash 3.2 (a Mac's) and bash 5.
#
# The failures that these functions report are counted in a FILE, HANG_LOG (one line each), and not in a variable: a function that runs inside $( ) (db_start, and stop_server in the command substitution of a check)
# is a subshell, and a counter that it changes is lost with it, so a script could print HUNG lines and exit 0. The caller sets HANG_LOG to a file in its scratch folder; without one the count is a variable, which
# is all a function outside every $( ) needs.
HANGS=0
HANG_LOG="${HANG_LOG:-}"
: "${STOP_WAIT:=15}"        # seconds that stop_pids gives all the processes together (a test shortens it)
: "${SERVER_END_WAIT:=30}"  # seconds that a server gets to end after SIGTERM (the e2e script's stop_server; a test shortens it)
: "${KILL_GRACE:=30}"       # tenths of a second between the SIGTERM and the SIGKILL of a process that is a hang (a test shortens it)
: "${WATCHDOG_GRACE:=10}"   # seconds between the watchdog's SIGUSR2 and its SIGKILL of what the script started (a test shortens it)

# hang_note TEXT: one more failure that is not a check
hang_note() {
    if [ -n "$HANG_LOG" ]; then echo "$1" >> "$HANG_LOG"; else HANGS=$((HANGS + 1)); fi
}

# hang_count: how many were noted, also by functions that ran inside a $( )
hang_count() {
    if [ -n "$HANG_LOG" ] && [ -f "$HANG_LOG" ]; then wc -l < "$HANG_LOG" | tr -d ' '; else echo "$HANGS"; fi
}

# fail_note TEXT: a line "FAIL: TEXT" on stderr (a function inside $( ) has no other way to be heard) that is counted as a failure although no check printed it
fail_note() {
    echo "  FAIL:${PART_LABEL} $1" >&2
    hang_note "$1"
}

# where: the lines of the calling script (not of this file) that led to the function that asks, "test_ants_server.sh:77 test_ants_server.sh:612" (a stop_server defined on line 77 and called on line 612)
where() {
    local i out=""
    for ((i = 0; i < ${#FUNCNAME[@]} - 1; i++)); do
        case "${BASH_SOURCE[i + 1]}" in *e2e_deadlines.sh) ;; *) out="$out ${BASH_SOURCE[i + 1]##*/}:${BASH_LINENO[i]}" ;; esac
    done
    echo "${out# }"
}

# hung WHAT SECONDS: WHAT was still running SECONDS after it was told to end (a failure: fail_note)
hung() {
    fail_note "HUNG: $1 was still running $2 s after it was told to end (it was killed) [called from $(where)]"
}

# kill_hung PID SECONDS WHAT: the hang is reported, the process is continued (a stopped one cannot die), told again with SIGTERM and, 3 s later, killed
kill_hung() {
    local pid="$1" i
    hung "$3" "$2"
    disown "$pid" 2> /dev/null      # (the shell then says nothing of its death: "Killed" would be a second line for the same hang)
    kill -CONT "$pid" 2> /dev/null
    kill -TERM "$pid" 2> /dev/null
    for ((i = 0; i < KILL_GRACE; i++)); do kill -0 "$pid" 2> /dev/null || break; sleep 0.1; done
    kill -KILL "$pid" 2> /dev/null
    for ((i = 0; i < 30; i++)); do kill -0 "$pid" 2> /dev/null || break; sleep 0.1; done
}

# wait_end PID SECONDS WHAT: waits for a child of this script to end, SECONDS at the most; its exit status (what `wait` says), 124 when it was a hang
wait_end() {
    local pid="$1" limit="$2" i
    for ((i = 0; i < limit * 10; i++)); do
        if ! kill -0 "$pid" 2> /dev/null; then wait "$pid" 2> /dev/null; return $?; fi
        sleep 0.1
    done
    kill_hung "$pid" "$limit" "$3"
    return 124
}

# stop_pids WHAT PID...: tells the children to end (SIGTERM; a stopped one is continued first) and waits for all of them together, STOP_WAIT s at the most
stop_pids() {
    local what="$1" p i left
    shift
    for p in "$@"; do
        disown "$p" 2> /dev/null      # (bash 3.2 and 5 say "Terminated" or "Killed" of a job that died of a signal; these die of ours, and are polled, not waited for)
        kill -CONT "$p" 2> /dev/null
        kill "$p" 2> /dev/null
    done
    for ((i = 0; i < STOP_WAIT * 10; i++)); do
        left=0
        for p in "$@"; do kill -0 "$p" 2> /dev/null && left=1; done
        [ "$left" = "0" ] && break
        sleep 0.1
    done
    for p in "$@"; do
        if kill -0 "$p" 2> /dev/null; then kill_hung "$p" "$STOP_WAIT" "$what (process $p)"; fi
    done
}

# run_limited SECONDS WHAT OUTPUT-FILE COMMAND...: runs a program that has to end by itself (node, replay_tool), its output in OUTPUT-FILE, SECONDS at the most; its exit status, 124 for a hang
run_limited() {
    local limit="$1" what="$2" out="$3"
    shift 3
    "$@" > "$out" 2>&1 &
    wait_end "$!" "$limit" "$what"
}

# start_watchdog SECONDS: the last line of defence. A part that has not ended after SECONDS fails, with a line that says which of its programs were still running, and exits with status 1. SIGUSR2 is the watchdog's
# word (a SIGTERM from outside, a cancelled job, is not it); when the script does not act on it (it is busy in a program in the foreground, which the shell runs to its end first) everything that it started is killed
# WATCHDOG_GRACE s later, which ends that program. The watchdog is never killed: it counts seconds for as long as its flag file exists and its parent lives, and ends by itself when stop_watchdog removes the flag
# (a bash 3.2, a Mac's, prints "Terminated: 15 ( ... )" on the script's stderr for a job that dies of a signal, and did so when the part ended before the watchdog had set up its own handler). It is disowned too:
# a job that is not in the shell's table cannot be reported.
on_watchdog() {
    local running pid
    trap - USR2
    running="$(jobs -p | tr '\n' ',' | sed 's/,$//')"
    echo "  FAIL:${PART_LABEL} HUNG: the part has not ended after $WATCHDOG_LIMIT s; the programs it started and did not stop (pid, command): ${running:-none}" >&2
    [ -n "$running" ] && ps -o pid=,command= -p "$running" 2> /dev/null | cut -c1-200 >&2
    echo "server e2e${PART_LABEL}: $CHECKS checks, $((FAILS + $(hang_count) + 1)) failures"
    for pid in $(jobs -p); do kill -CONT "$pid" 2> /dev/null; kill -KILL "$pid" 2> /dev/null; done       # (nothing it started outlives it: a program that holds the step's output open would hang the step again)
    exit 1
}
start_watchdog() {
    WATCHDOG_LIMIT="$1"
    WATCHDOG_FLAG="$(mktemp "${TMPDIR:-/tmp}/ants_watchdog.XXXXXX")"
    trap on_watchdog USR2
    (
        n=0
        while [ -e "$WATCHDOG_FLAG" ] && kill -0 $$ 2> /dev/null && [ "$n" -lt "$1" ]; do sleep 1; n=$((n + 1)); done
        [ -e "$WATCHDOG_FLAG" ] && kill -0 $$ 2> /dev/null || exit 0
        kill -USR2 $$
        n=0
        while [ -e "$WATCHDOG_FLAG" ] && kill -0 $$ 2> /dev/null && [ "$n" -lt "$WATCHDOG_GRACE" ]; do sleep 1; n=$((n + 1)); done
        [ -e "$WATCHDOG_FLAG" ] && pkill -KILL -P $$
        exit 0
    ) > /dev/null 2>&1 &
    WATCHDOG_PID=$!
    disown "$WATCHDOG_PID" 2> /dev/null
}
# stop_watchdog: the part has ended in time; the watchdog sees that its flag is gone within a second and ends by itself
stop_watchdog() {
    [ -n "$WATCHDOG_FLAG" ] && rm -f "$WATCHDOG_FLAG"
    WATCHDOG_FLAG=""
    WATCHDOG_PID=""
}
