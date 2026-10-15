# =============================================================================
# Stage 1: Build WebAssembly Target via Emscripten SDK
# =============================================================================
FROM emscripten/emsdk:3.1.58 AS builder

WORKDIR /src

# Copy build files and source code (VERSION is the one source of the version: CMake generates the game's version header from it)
COPY CMakeLists.txt VERSION ./
COPY cmake/ ./cmake/
COPY include/ ./include/
COPY src/ ./src/
COPY web/ ./web/
COPY Original-Ants/ ./Original-Ants/

# Inject the build timestamp (JS, WASM and data bundles share lockstep versioning) and the game's version text (the file VERSION) into shell.html (the game page), lobby.html (the front page: its footer) and watch.html (the list of the replays: its footer)
RUN BUILD_TIME=$(date +%s) && \
    GAME_VERSION="v$(head -n 1 VERSION | tr -d '[:space:]')" && \
    case "${GAME_VERSION}" in v[0-9]*.[0-9]*.[0-9]*) ;; *) echo "VERSION is not MAJOR.MINOR.PATCH: ${GAME_VERSION}" >&2; exit 1 ;; esac && \
    sed -i "s/@@BUILD_TIMESTAMP@@/${BUILD_TIME}/g; s/@@GAME_VERSION@@/${GAME_VERSION}/g" web/shell.html web/lobby.html web/watch.html && \
    ! grep -q '@@GAME_VERSION@@' web/shell.html web/lobby.html web/watch.html

# Configure and compile using default Makefiles
RUN emcmake cmake -B build_web \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

RUN cmake --build build_web -j$(nproc)

# Append dynamic build cache-buster to the index.js script tag (only a <script> tag: the page adds the ?v= to its own script address itself, and a sed over every `src=index.js`
# gave that one a second ?v=) to prevent stale browser caching, and write the exact size of index.data into the page (the page refuses a download of another size: a captive portal's
# web page that answers "200 OK"). The build stops if either did not work.
RUN BUILD_TIME=$(date +%s) && \
    PAGE=/src/build_web/src/ants_app/index.html && \
    sed -i -E 's/(<script[^>]* src=)("?)index\.js("?)/\1\2index.js?v='"${BUILD_TIME}"'\3/g' "$PAGE" && \
    DATA_SIZE=$(stat -c %s /src/build_web/src/ants_app/index.data) && \
    sed -i "s/@@DATA_SIZE@@/${DATA_SIZE}/g" "$PAGE" && \
    grep -q "index.js?v=${BUILD_TIME}" "$PAGE" && \
    ! grep -q '@@DATA_SIZE@@' "$PAGE" && \
    ! grep -Eq 'index\.js\?v=[0-9]+\?v=' "$PAGE"

# Which build this is (the page's footer and the footer of the changelog page name it), found AFTER the compile layers so that a new commit does not recompile the game
# (the page is the only place of the web build that shows it; the compiled game's own ants::BUILD_ID is "unknown" here and nothing in the browser prints it). The first of:
# the build argument ANTS_BUILD_ID (docker build --build-arg ANTS_BUILD_ID=$(git rev-parse --short HEAD) .; a compose file's build.args), the commit named by the HEAD and refs files
# of the repository's .git folder (a stack that a deployment tool builds from a clone has them: .dockerignore lets only these three through, never .git/config), the UTC build time.
# (`COPY VERSION .git* ...` copies VERSION, the two files .gitattributes / .gitignore and, when the context has a .git folder, the three entries that .dockerignore lets through; it works with and without a .git folder.)
# docker/resolve_build_id.sh says which one it used in the build log.
ARG ANTS_BUILD_ID=
ARG ANTS_SITE_LABEL=
COPY docker/resolve_build_id.sh /src/docker/resolve_build_id.sh
COPY VERSION .git* /src/gitinfo/
RUN BUILD_ID="$(sh /src/docker/resolve_build_id.sh "${ANTS_BUILD_ID}" /src/gitinfo)" && \
    BUILD_DATE="$(TZ=PST8PDT,M3.2.0,M11.1.0 date '+%Y-%m-%d %H:%M PT')" && \
    echo "${BUILD_ID}" > /src/build_id.txt && \
    echo "${BUILD_DATE}" > /src/build_date.txt && \
    PAGE=/src/build_web/src/ants_app/index.html && \
    sed -i "s/@@BUILD_ID@@/${BUILD_ID}/g; s/@@BUILD_DATE@@/${BUILD_DATE}/g" "$PAGE" /src/web/lobby.html /src/web/watch.html && \
    ! grep -q '@@BUILD_' "$PAGE" /src/web/lobby.html /src/web/watch.html && \
    grep -Eq "id=\"?game-build-id\"?>${BUILD_ID}<" "$PAGE" && \
    grep -Eq "id=\"?game-build-id\"?>${BUILD_ID}<" /src/web/lobby.html && \
    grep -Eq "id=\"?game-build-id\"?>${BUILD_ID}<" /src/web/watch.html && \
    grep -Eq "id=\"?game-build-date\"?>\\(${BUILD_DATE}\\)<" "$PAGE" && \
    grep -Eq "id=\"?game-build-date\"?>\\(${BUILD_DATE}\\)<" /src/web/lobby.html && \
    grep -Eq "id=\"?game-build-date\"?>\\(${BUILD_DATE}\\)<" /src/web/watch.html

# The site label (the build argument ANTS_SITE_LABEL, empty for the production site, "staging" for the staging stack: docker-compose.staging.yml): a non-empty label is put in the title
# and in the footer of the game page and of the front page (the lobby), so that nobody takes the staging site for the production one. Empty, the two placeholders become nothing and the
# pages are the same as ever (the same layer as the build id: the compile layers above do not depend on it). A label may hold letters, digits, dot, dash and underscore only.
RUN SITE_LABEL="${ANTS_SITE_LABEL}" && \
    case "${SITE_LABEL}" in *[!A-Za-z0-9._-]*) echo "ANTS_SITE_LABEL may hold letters, digits, dot, dash and underscore only: ${SITE_LABEL}" >&2; exit 1 ;; esac && \
    if [ -n "${SITE_LABEL}" ]; then SITE_TITLE=" (${SITE_LABEL})"; SITE_FOOTER="<strong id=\"site-label\">${SITE_LABEL}</strong>\&#8197;\&bull;\&#8197;"; else SITE_TITLE=""; SITE_FOOTER=""; fi && \
    cp /src/web/lobby.html /src/lobby.html && \
    cp /src/web/watch.html /src/watch.html && \
    for PAGE in /src/build_web/src/ants_app/index.html /src/lobby.html /src/watch.html; do \
        sed -i "s|@@SITE_TITLE@@|${SITE_TITLE}|g; s|@@SITE_FOOTER@@|${SITE_FOOTER}|g" "$PAGE" || exit 1; \
        if grep -q '@@SITE_' "$PAGE"; then echo "a site placeholder is left in $PAGE" >&2; exit 1; fi; \
    done

# Build the changelog page (CHANGELOG.md -> changelog.html; no dependencies) after the compile layers so that editing the changelog does not rebuild the game
COPY CHANGELOG.md /src/changelog/CHANGELOG.md
COPY tools/changelog_to_html.py /src/changelog/changelog_to_html.py
RUN cd /src/changelog && \
    GAME_VERSION="v$(head -n 1 /src/VERSION | tr -d '[:space:]')" && \
    BUILD_ID="$(cat /src/build_id.txt)" && \
    BUILD_DATE="$(cat /src/build_date.txt)" && \
    python3 changelog_to_html.py CHANGELOG.md changelog.html --version "${GAME_VERSION}" --build-id "${BUILD_ID}" --build-date "${BUILD_DATE}"

# =============================================================================
# Stage 2: High-Performance Lightweight Nginx Web Server
# =============================================================================
FROM nginx:alpine AS runner

# Clear default static files
RUN rm -rf /usr/share/nginx/html/*

# Copy compiled WebAssembly artifacts atomically in a single layer
COPY --from=builder /src/build_web/src/ants_app/index.* /usr/share/nginx/html/

# The game page also answers at a path of its own, play.html (the games on this computer: START for one on the front page opens it with ?map=...): a copy of index.html. nginx serves "/" with
# the lobby unless the address is a game's (?join=: docker/nginx.conf), so index.html is still what every old game address opens.
RUN cp /usr/share/nginx/html/index.html /usr/share/nginx/html/play.html

# Copy favicon assets
COPY web/favicon.* /usr/share/nginx/html/

# The pictures and the font of the front page (web/front/, served at /front/): the game's own art, made by tools/front_page_art/make_art.py, and its own font with its licence
COPY web/front/ /usr/share/nginx/html/front/

# The front page, the lobby (lobby.html): a room on the game server that is ready when the page opens (a link to send, a code to type, START), or a game for one on this computer,
# with the site label, the version and the build put in. nginx serves it at "/"; the old address /four.html redirects there.
COPY --from=builder /src/lobby.html /usr/share/nginx/html/lobby.html

# The players' maps for the browser (docs/SERVER.md "The list of maps"): the preview pictures (534 small WebP files) and the .lvl files themselves, served by docker/nginx.conf at /maps/previews/ and /maps/files/ from
# a folder outside the web root, so that only those two locations can reach them. The six originals' pictures are the front page's own (web/front/): no second copy.
COPY Community-Maps/previews/ /srv/community-maps/previews/
COPY Community-Maps/*.lvl Community-Maps/*.LVL /srv/community-maps/files/

# The replays (docs/REPLAYS.md "Watching"): the list of the matches that the game server keeps (watch.html, linked from the footer of the front page and of the game page, with the site label, the version and the
# build put in), and the helpers that it shares with the game page (replay_page.js). The game page plays one as play.html?replay=<file>.
COPY --from=builder /src/watch.html /usr/share/nginx/html/watch.html
COPY web/replay_page.js /usr/share/nginx/html/replay_page.js

# The players' maps (docs/PLAY_IN_BROWSER.md "Players' maps in the browser"): the game page fetches the map of a room or a recording from /maps/files/ (map_files.js)
COPY web/map_files.js /usr/share/nginx/html/map_files.js

# The countdown's Fullscreen button (docs/MATCH_COUNTDOWN.md): the game page lays it over the card that opens a match of two or more people (countdown_button.js)
COPY web/countdown_button.js /usr/share/nginx/html/countdown_button.js

# The Legal page (docs/PLAY_IN_BROWSER.md "The Legal page"): a page of its own in the changelog page's look, linked from every footer, with no placeholder to fill in
COPY web/legal.html /usr/share/nginx/html/legal.html

# The changelog page (built from CHANGELOG.md, linked from the game page); it is in the Classic look of the front page: it links
# /front/classic.css and shows /front/logo.png, which the web/front/ copy above puts in the image
COPY --from=builder /src/changelog/changelog.html /usr/share/nginx/html/

# Copy Asset Catalog & Viewer for reference on beta site
COPY asset_catalog/ /usr/share/nginx/html/asset_catalog/

# Copy custom Nginx configuration
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

HEALTHCHECK --interval=10s --timeout=3s --start-period=3s --retries=3 \
    CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1

CMD ["nginx", "-g", "daemon off;"]
