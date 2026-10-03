#!/bin/sh
# Prints the build id of a Docker image build (one word on stdout; where it came from on stderr). Used by Dockerfile and Dockerfile.server, tested by
# tests/scripts/test_version_tools.py.
#
#   resolve_build_id.sh [EXPLICIT_ID] [GIT_INFO_DIR]
#
# 1. EXPLICIT_ID (the build argument ANTS_BUILD_ID: `docker build --build-arg ANTS_BUILD_ID=$(git rev-parse --short HEAD) .`), when it is a valid id.
# 2. The short commit that GIT_INFO_DIR names: the files HEAD, packed-refs and refs/ of the repository's .git folder, as the Dockerfiles copy them (see .dockerignore:
#    only these three are part of the build context, never .git/config, which can hold a password in a remote's address). A stack that a deployment tool builds from
#    a clone of the repository (Portainer's "Repository" stack) has no git program in the image and no build argument, but it has these files.
#    The Dockerfiles copy it with `COPY VERSION .git* <dir>/` (a copy of those three entries: HEAD, packed-refs, refs/), so refs/heads/main is found as <dir>/refs/heads/main;
#    <dir>/heads/main is looked for too (a copy of the contents of refs/ on its own).
# 3. The UTC time of the build, YYYYMMDD-HHMM: an image that was built from a source archive (no .git at all) is still told apart from the one before it.
#
# A valid id is 1 - 40 characters of letters, digits, dot, dash and underscore.

explicit="$1"
dir="$2"

valid() {
    case "$1" in
        '' ) return 1 ;;
        *[!A-Za-z0-9._-]* ) return 1 ;;
    esac
    [ "${#1}" -le 40 ]
}

hex_commit() {
    # 40 (SHA-1) or 64 (SHA-256) hexadecimal digits, nothing else
    case "$1" in
        *[!0-9a-f]* | '' ) return 1 ;;
    esac
    [ "${#1}" -eq 40 ] || [ "${#1}" -eq 64 ]
}

if valid "$explicit"; then
    echo "build id from the build argument" >&2
    printf '%s\n' "$explicit"
    exit 0
fi

if [ -n "$dir" ] && [ -f "$dir/HEAD" ]; then
    head_line=$(head -n 1 "$dir/HEAD" 2>/dev/null | tr -d '\r')
    commit=""
    case "$head_line" in
        "ref: "*)
            ref="${head_line#ref: }"
            for candidate in "$dir/$ref" "$dir/${ref#refs/}"; do
                if [ -f "$candidate" ]; then
                    commit=$(head -n 1 "$candidate" 2>/dev/null | tr -d '\r')
                    break
                fi
            done
            if [ -z "$commit" ] && [ -f "$dir/packed-refs" ]; then
                commit=$(awk -v ref="$ref" '$2 == ref && $1 !~ /^[#^]/ { print $1; exit }' "$dir/packed-refs" | tr -d '\r')
            fi
            ;;
        *)
            commit="$head_line"       # a detached HEAD holds the commit itself
            ;;
    esac
    if hex_commit "$commit"; then
        echo "build id from the git commit" >&2
        printf '%s\n' "$commit" | cut -c1-7
        exit 0
    fi
fi

echo "build id from the build date (no build argument, no git information)" >&2
date -u +%Y%m%d-%H%M
