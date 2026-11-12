#!/bin/sh
# Lists what the link of the web image reads, for tools/image_files_diff.sh. It runs INSIDE the builder stage of a web image, read from its standard input:
#   docker run --rm -i IMAGE sh -s < tools/link_inputs.sh
# One line per file, sorted: "file", the path, the sha256 of the content (every object file, archive, link command file, compile flags file and generated header of the CMake build folder, and every library
# archive in Emscripten's cache, system libraries and ports), then one "link" line for each link command file with the command line itself (so that a difference is readable). Two builds that give the
# same listing link the same objects and libraries with the same command lines. Fields are separated by a tab. (ANTS_SRC names another source folder than /src: for the tests.)
cd "${ANTS_SRC:-/src}" || exit 2
cache="$(em-config CACHE 2> /dev/null)"
[ -d "$cache/sysroot/lib" ] || cache=/emsdk/upstream/emscripten/cache
[ -d "$cache/sysroot/lib" ] || { echo "link_inputs: no Emscripten cache folder" >&2; exit 2; }
[ -d build_web ] || { echo "link_inputs: no build_web folder in $(pwd)" >&2; exit 2; }
{
    find build_web -type f \( -name '*.o' -o -name '*.a' -o -name link.txt -o -name flags.make \)
    find build_web/generated -type f
    find "$cache/sysroot/lib" -type f -name '*.a'
} | LC_ALL=C sort | while IFS= read -r f; do
    printf 'file\t%s\t%s\n' "$f" "$(sha256sum < "$f" | cut -d ' ' -f 1)"
done
find build_web -type f -name link.txt | LC_ALL=C sort | while IFS= read -r f; do
    printf 'link\t%s\t%s\n' "$f" "$(tr '\n' ' ' < "$f")"
done
