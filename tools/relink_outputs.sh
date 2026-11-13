#!/bin/sh
# Links the web image's programs again from the objects of its builder stage and prints their sha256, for tools/image_files_diff.sh. It runs INSIDE the builder stage, as
#   docker run --rm -i --security-opt seccomp=unconfined -e BINARYEN_CORES=1 -e EMCC_CFLAGS=-Wl,--threads=1 IMAGE setarch x86_64 -R sh -s < tools/relink_outputs.sh
# so that the link does not depend on the address space layout (setarch -R) or on the timing of threads (one thread for the linker, one for the optimizer), and so that Emscripten puts the game's four
# port libraries (SDL2, SDL2_ttf, FreeType, HarfBuzz) on the linker's command line in one fixed order. Emscripten takes that order from a Python set of the ports' modules, which orders them by memory
# address: three orders are allowed by the ports' dependencies, each gives another data layout, so another index.wasm and other import names in index.js, and which one a link gets depends on the
# address space layout and on the size of the environment. This script makes Emscripten take the port with the smallest name first, in this throw-away container only (the image and production
# builds are not touched), and stops with status 2 when the line to change is not there (another Emscripten: the fix would be silently missing). The same objects then give the same programs every
# time. That holds only in the check, never in the image's own build. Lines: "file", the name, the sha256 (index.js, index.wasm, lvl_core.js, lvl_core.wasm).
cd "${ANTS_SRC:-/src}" || exit 2
game=build_web/src/ants_app
core=build_web/lvl_core
[ -d "$game" ] && [ -d "$core" ] || { echo "relink_outputs: no build folders in $(pwd)" >&2; exit 2; }
ports="${ANTS_PORTS_FILE:-$(dirname "$(readlink -f "$(command -v emcc)")")/tools/ports/__init__.py}"
[ -f "$ports" ] && [ "$(grep -c '^ *dfs(unsorted.pop())$' "$ports")" = 1 ] || { echo "relink_outputs: the line dfs(unsorted.pop()) is not in $ports exactly once: this Emscripten orders its ports in another way, and this check has to be changed for it" >&2; exit 2; }
sed 's/^\( *\)dfs(unsorted.pop())$/\1port = min(unsorted, key=lambda p: p.name); unsorted.remove(port); dfs(port)/' "$ports" > "$ports.new" && cat "$ports.new" > "$ports" && rm -f "$ports.new" || { echo "relink_outputs: the order of the ports could not be fixed" >&2; exit 2; }
[ "$(grep -c 'port = min(unsorted, key=lambda p: p.name); unsorted.remove(port); dfs(port)$' "$ports")" = 1 ] || { echo "relink_outputs: the order of the ports was not fixed" >&2; exit 2; }
rm -f "$game/index.html" "$game/index.js" "$game/index.wasm" "$game/index.data" "$core/lvl_core.js" "$core/lvl_core.wasm"
cmake --build build_web -j1 > /dev/null || { echo "relink_outputs: the link failed" >&2; exit 2; }
for f in "$game/index.js" "$game/index.wasm" "$core/lvl_core.js" "$core/lvl_core.wasm"; do
    [ -s "$f" ] || { echo "relink_outputs: $f was not made" >&2; exit 2; }
    printf 'file\t%s\t%s\n' "$(basename "$f")" "$(sha256sum < "$f" | cut -d ' ' -f 1)"
done
