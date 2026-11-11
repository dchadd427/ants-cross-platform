#!/usr/bin/env bash
# Proves that a change of a Dockerfile leaves the files of the images alone: builds the web image and the game server's image twice with the same build context and the same build id, once from the
# Dockerfile of a base revision (default origin/main) and once from the Dockerfile of this tree, exports the final file system of each and compares every file (type, mode, owner, size, link target and
# sha256 of the content), then the image's configuration (user, entrypoint, command, ports, environment, working folder, health check). Any difference fails; the only text that may differ is the
# build time and the build date of the pages (tools/image_manifest.py: STAMPED, a short list of files and patterns).
#
#   tools/image_files_diff.sh [--base REV] [--web] [--server]      (both images when neither is named; REV: any revision that has Dockerfile and Dockerfile.server, default origin/main)
#
# Needs Docker with BuildKit, git and python3, and about 4 GB of disk. The two builds of the web image take 6 to 10 minutes each on a 4-core machine (the base Dockerfile has no cache of ours).
# Run from the root of the repository. Exit status 0: the same; 1: a difference (listed); 2: something could not be built or read.
set -u

base=origin/main
want_web=0
want_server=0
while [ $# -gt 0 ]; do
    case "$1" in
        --base) base="${2:-}"; shift 2 || { echo "image_files_diff: --base wants a revision" >&2; exit 2; } ;;
        --web) want_web=1; shift ;;
        --server) want_server=1; shift ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "image_files_diff: unknown argument $1" >&2; exit 2 ;;
    esac
done
if [ "$want_web" = 0 ] && [ "$want_server" = 0 ]; then want_web=1; want_server=1; fi

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root" || exit 2
for need in docker git python3; do
    command -v "$need" > /dev/null 2>&1 || { echo "image_files_diff: $need is not installed" >&2; exit 2; }
done
export DOCKER_BUILDKIT=1
work="$(mktemp -d)"
tag="ants-image-diff-$$"
containers=""
cleanup() {
    for c in $containers; do docker rm -f "$c" > /dev/null 2>&1; done
    docker image rm -f "$tag-web-base" "$tag-web-new" "$tag-server-base" "$tag-server-new" > /dev/null 2>&1
    rm -rf "$work"
}
trap cleanup EXIT

status=0
fail=0

# compare NAME DOCKERFILE-NAME: build the base's and this tree's file with the same context and the same build id, then compare
compare() {
    name="$1"
    file="$2"
    git show "$base:$file" > "$work/$file.base" 2> "$work/git.err" || { echo "image_files_diff: $base has no $file: $(cat "$work/git.err")" >&2; return 2; }
    for which in base new; do
        dockerfile="$file"
        [ "$which" = base ] && dockerfile="$work/$file.base"
        echo "== building $name image from the $which Dockerfile ($dockerfile)"
        docker build -f "$dockerfile" --build-arg ANTS_BUILD_ID=image-diff -t "$tag-$name-$which" . || { echo "image_files_diff: the $which build of the $name image failed" >&2; return 2; }
        cid="$(docker create "$tag-$name-$which")" || return 2
        containers="$containers $cid"
        docker export "$cid" | python3 tools/image_manifest.py manifest > "$work/$name-$which.manifest" || { echo "image_files_diff: the file list of the $which $name image could not be made" >&2; return 2; }
        docker image inspect "$tag-$name-$which" | python3 tools/image_manifest.py config > "$work/$name-$which.config" || return 2
        docker rm -f "$cid" > /dev/null 2>&1
    done
    rc=0
    python3 tools/image_manifest.py compare "$work/$name-base.manifest" "$work/$name-new.manifest" --label "$name files" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    [ "$rc" = 1 ] && fail=1
    rc=0
    python3 tools/image_manifest.py compare "$work/$name-base.config" "$work/$name-new.config" --label "$name configuration" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    [ "$rc" = 1 ] && fail=1
    return 0
}

if [ "$want_web" = 1 ]; then compare web Dockerfile || status=2; fi
if [ "$want_server" = 1 ]; then compare server Dockerfile.server || status=2; fi
if [ "$status" != 0 ]; then echo "image_files_diff: could not finish" >&2; exit 2; fi
if [ "$fail" != 0 ]; then echo "image_files_diff: the images DIFFER (listed above)" >&2; exit 1; fi
echo "image_files_diff: the images are the same (every file and the configuration), apart from the build time stamps of the pages"
exit 0
