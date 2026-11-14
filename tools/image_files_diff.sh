#!/usr/bin/env bash
# Proves that a change of a Dockerfile leaves the images alone: builds the web image and the game server's image twice with the same build context and the same build id, once from the
# Dockerfile of a base revision (a pull request's base) and once from the Dockerfile of this tree, exports the final file system of each and compares every file (type, mode, owner, size, link target and
# sha256 of the content), then the image's configuration (user, entrypoint, command, ports, environment, working folder, health check). Any difference fails; the only text that may differ is the
# build time and the build date of the pages (tools/image_manifest.py: STAMPED, a short list of files and patterns).
#
# The linked WebAssembly programs and their JavaScript are the exception: two links of the very same objects do not always give the same bytes (Emscripten puts the
# game's four port libraries on the linker's command line in an order that it takes from a Python set: every normal build links with one of three orders, and the three programs are the same
# objects in another layout), so a comparison of those files could only say "the two builds hit the same variant". For the web image they are compared where it is exact:
#   1. the link's inputs, from the builder stage of both builds (tools/link_inputs.py: every object file, archive, response file (the lists of objects and libraries that the link command lines
#      name) and library that the link reads, the start-up objects of Emscripten's cache, and the link command lines), byte for byte (a library or object of Emscripten's cache by its members
#      without their debug information: the ports' debug information holds the folder of their compile, which differs between the two Dockerfiles and which the link drops);
#   2. the programs of a link that is made deterministic inside the check only (tools/relink_outputs.sh: the ports in one fixed order, no address randomization, one linker thread, one optimizer thread), unless --no-relink; if
#      two such links of one image differ here the recipe does not hold on this machine, which is said, and only the first comparison counts.
# The image's own files of those programs keep their type, mode, owner and a size within one percent, and each of the six (index.js, index.wasm, their two .gz copies, lvl_core.js, lvl_core.wasm) is
# the builder stage's own link output, byte for byte (a copy of it: nothing after the link may have edited it, and a stub link's file of about the same size cannot stand in). Production links as it
# always did.
#
#   tools/image_files_diff.sh [--base REV] [--web] [--server] [--diagnose] [--no-relink]      (both images when neither is named; REV: any revision that has Dockerfile and Dockerfile.server, default origin/main)
#   --diagnose: when the web images differ, also show how the differing files differ
#
# Needs Docker with BuildKit, git and python3, and about 4 GB of disk. The two builds of the web image take 6 to 10 minutes each on a 4-core machine (the base Dockerfile has no cache of ours).
# Run from the root of the repository. Exit status 0: the same; 1: a difference (listed); 2: something could not be built or read.
set -u

base=origin/main
want_web=0
want_server=0
diagnose=0
relink=1
while [ $# -gt 0 ]; do
    case "$1" in
        --base) base="${2:-}"; shift 2 || { echo "image_files_diff: --base wants a revision" >&2; exit 2; } ;;
        --web) want_web=1; shift ;;
        --server) want_server=1; shift ;;
        --diagnose) diagnose=1; shift ;;
        --no-relink) relink=0; shift ;;
        -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
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
    docker image rm -f "$tag-web-base" "$tag-web-new" "$tag-server-base" "$tag-server-new" "$tag-web-builder-base" "$tag-web-builder-new" > /dev/null 2>&1
    rm -rf "$work"
}
trap cleanup EXIT

status=0
fail=0
relink_held=1

# show_diff A B: how two files differ (byte count, the first bytes, the text that they hold)
show_diff() {
    echo "---- $3: $(cmp -l "$1" "$2" 2> /dev/null | wc -l) bytes differ ($(wc -c < "$1") and $(wc -c < "$2") bytes)"
    cmp -l "$1" "$2" 2> /dev/null | head -8
    diff <(strings -n 6 "$1") <(strings -n 6 "$2") | cut -c1-300 | head -40
}

# diagnose_web DOCKERFILE: how the differing files of the web images differ
diagnose_web() {
    mkdir -p "$work/dx"
    for f in index.html; do
        docker cp "$base_cid:/usr/share/nginx/html/$f" "$work/dx/base-$f" && docker cp "$new_cid:/usr/share/nginx/html/$f" "$work/dx/new-$f" && show_diff "$work/dx/base-$f" "$work/dx/new-$f" "$f"
    done
}

# tie_outputs WHICH: the six linked files of the final image are the builder stage's own link outputs, byte for byte (the final image's copies come from the builder stage by COPY, so this holds
# unless a step after the link edited one, or the Dockerfile copies another file)
tie_outputs() {
    which="$1"
    cid="$base_cid"
    [ "$which" = new ] && cid="$new_cid"
    docker run --rm "$tag-web-builder-$which" sh -c 'cd /src/build_web && for f in src/ants_app/index.js src/ants_app/index.js.gz src/ants_app/index.wasm src/ants_app/index.wasm.gz lvl_core/lvl_core.js lvl_core/lvl_core.wasm; do printf "%s\t%s\n" "$(basename "$f")" "$(sha256sum < "$f" | cut -d " " -f 1)"; done' > "$work/web-built-$which.txt" \
        || { echo "image_files_diff: the link outputs of the $which builder stage could not be listed" >&2; return 2; }
    mkdir -p "$work/final-$which"
    : > "$work/web-final-$which.txt"
    for f in index.js index.js.gz index.wasm index.wasm.gz lvl_core.js lvl_core.wasm; do
        docker cp "$cid:/usr/share/nginx/html/$f" "$work/final-$which/$f" || { echo "image_files_diff: $f could not be taken out of the $which web image" >&2; return 2; }
        printf '%s\t%s\n' "$f" "$(sha256sum < "$work/final-$which/$f" | cut -d ' ' -f 1)" >> "$work/web-final-$which.txt"
    done
    if ! cmp -s "$work/web-built-$which.txt" "$work/web-final-$which.txt"; then
        echo "web link outputs: the six linked files of the $which web image are not the builder stage's link outputs (builder stage on the left, image on the right):"
        diff "$work/web-built-$which.txt" "$work/web-final-$which.txt"
        fail=1
    else
        echo "web link outputs: the six linked files of the $which web image are the builder stage's link outputs, byte for byte"
    fi
    return 0
}

# web_link FILE-NAME: the link's inputs of the base's and of this tree's builder stage (a build that the layer cache answers), then the programs of a deterministic link of each
web_link() {
    file="$1"
    for which in base new; do
        dockerfile="$file"
        [ "$which" = base ] && dockerfile="$work/$file.base"
        echo "== the builder stage of the $which Dockerfile"
        docker build -f "$dockerfile" --target builder --build-arg ANTS_BUILD_ID=image-diff -t "$tag-web-builder-$which" . > /dev/null || { echo "image_files_diff: the builder stage of the $which Dockerfile could not be built" >&2; return 2; }
        docker run --rm -i "$tag-web-builder-$which" python3 - < tools/link_inputs.py > "$work/web-inputs-$which.txt" || { echo "image_files_diff: the link inputs of the $which build could not be listed" >&2; return 2; }
    done
    for which in base new; do tie_outputs "$which" || return 2; done
    rc=0
    python3 tools/image_manifest.py inputs "$work/web-inputs-base.txt" "$work/web-inputs-new.txt" --label "web link inputs" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    if [ "$rc" = 1 ]; then fail=1; fi
    [ "$relink" = 1 ] || { relink_held=0; echo "web link outputs: not compared (--no-relink)"; return 0; }
    # one image linked twice first: when the two disagree the recipe does not hold on this machine, and the inputs are all that is compared
    for which in base new new2; do
        image="$tag-web-builder-${which%2}"
        echo "== the programs of a deterministic link of the $which builder stage"
        docker run --rm -i --security-opt seccomp=unconfined -e BINARYEN_CORES=1 -e EMCC_CFLAGS=-Wl,--threads=1 "$image" setarch x86_64 -R sh -s < tools/relink_outputs.sh > "$work/web-relink-$which.txt" \
            || { echo "image_files_diff: the deterministic link of the $which build failed" >&2; return 2; }
    done
    if ! cmp -s "$work/web-relink-new.txt" "$work/web-relink-new2.txt"; then
        relink_held=0
        echo "web link outputs: WARNING: two deterministic links of one image gave different programs on this machine, so the recipe does not hold here; not compared (the link inputs above are)"
        echo "::warning title=The deterministic link did not hold::Two deterministic links of the new web image gave different programs, so the programs were not compared; the link inputs, the other files and the tie of the six linked files to the builder stage were."
        diff "$work/web-relink-new.txt" "$work/web-relink-new2.txt"
        return 0
    fi
    rc=0
    python3 tools/image_manifest.py relink "$work/web-relink-base.txt" "$work/web-relink-new.txt" --label "web link outputs" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    if [ "$rc" = 1 ]; then fail=1; fi
    return 0
}

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
        [ "$which" = base ] && base_cid="$cid"
        [ "$which" = new ] && new_cid="$cid"
    done
    rc=0
    python3 tools/image_manifest.py compare "$work/$name-base.manifest" "$work/$name-new.manifest" --label "$name files" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    if [ "$rc" = 1 ]; then
        fail=1
        [ "$diagnose" = 1 ] && [ "$name" = web ] && diagnose_web "$file"
    fi
    rc=0
    python3 tools/image_manifest.py compare "$work/$name-base.config" "$work/$name-new.config" --label "$name configuration" || rc=$?
    [ "$rc" -ge 2 ] && return 2
    [ "$rc" = 1 ] && fail=1
    if [ "$name" = web ]; then web_link "$file" || return 2; fi
    return 0
}

if [ "$want_web" = 1 ]; then compare web Dockerfile || status=2; fi
if [ "$want_server" = 1 ]; then compare server Dockerfile.server || status=2; fi
if [ "$status" != 0 ]; then echo "image_files_diff: could not finish" >&2; exit 2; fi
if [ "$fail" != 0 ]; then echo "image_files_diff: the images DIFFER (listed above)" >&2; exit 1; fi
if [ "$want_web" = 1 ] && [ "$relink_held" = 0 ]; then link_note="the web image's link outputs were NOT compared (see the warning above)"; else link_note="the web image's link outputs"; fi
echo "image_files_diff: the images are the same (every file and the configuration, the web image's link inputs and $link_note), apart from the build time stamps of the pages"
exit 0
