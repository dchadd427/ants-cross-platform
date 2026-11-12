#!/bin/sh
# Links the web image's programs again from the objects of its builder stage and prints their sha256, for tools/image_files_diff.sh. It runs INSIDE the builder stage, as
#   docker run --rm -i --security-opt seccomp=unconfined -e BINARYEN_CORES=1 -e EMCC_CFLAGS=-Wl,--threads=1 IMAGE setarch x86_64 -R sh -s < tools/relink_outputs.sh
# so that nothing of the link depends on the address space layout (setarch -R) or on the timing of threads (one thread for the linker, one for the optimizer): the same objects then give the same
# programs every time. That holds only in the check, never in the image's own build. Lines: "file", the name, the sha256 (index.js, index.wasm, lvl_core.js, lvl_core.wasm).
cd "${ANTS_SRC:-/src}" || exit 2
game=build_web/src/ants_app
core=build_web/lvl_core
[ -d "$game" ] && [ -d "$core" ] || { echo "relink_outputs: no build folders in $(pwd)" >&2; exit 2; }
rm -f "$game/index.html" "$game/index.js" "$game/index.wasm" "$game/index.data" "$core/lvl_core.js" "$core/lvl_core.wasm"
cmake --build build_web -j1 > /dev/null || { echo "relink_outputs: the link failed" >&2; exit 2; }
for f in "$game/index.js" "$game/index.wasm" "$core/lvl_core.js" "$core/lvl_core.wasm"; do
    [ -s "$f" ] || { echo "relink_outputs: $f was not made" >&2; exit 2; }
    printf 'file\t%s\t%s\n' "$(basename "$f")" "$(sha256sum < "$f" | cut -d ' ' -f 1)"
done
