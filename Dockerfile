# =============================================================================
# Stage 1: Build WebAssembly Target via Emscripten SDK
# =============================================================================
FROM emscripten/emsdk:3.1.58 AS builder

WORKDIR /src

# Copy build files and source code
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY web/ ./web/
COPY Original-Ants/ ./Original-Ants/

# Inject dynamic build timestamp into shell.html so JS, WASM and data bundles share lockstep versioning
RUN BUILD_TIME=$(date +%s) && \
    sed -i "s/@@BUILD_TIMESTAMP@@/${BUILD_TIME}/g" web/shell.html

# Configure and compile using default Makefiles
RUN emcmake cmake -B build_web \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

RUN cmake --build build_web -j$(nproc)

# Append dynamic build cache-buster to index.js script tag to prevent stale browser caching
RUN BUILD_TIME=$(date +%s) && \
    sed -i -E 's/(src=)("?)index\.js("?)/\1\2index.js?v='"${BUILD_TIME}"'\3/g' /src/build_web/src/ants_app/index.html

# =============================================================================
# Stage 2: High-Performance Lightweight Nginx Web Server
# =============================================================================
FROM nginx:alpine AS runner

# Clear default static files
RUN rm -rf /usr/share/nginx/html/*

# Copy compiled WebAssembly artifacts atomically in a single layer
COPY --from=builder /src/build_web/src/ants_app/index.* /usr/share/nginx/html/

# Copy favicon assets
COPY web/favicon.* /usr/share/nginx/html/

# Copy Asset Catalog & Viewer for reference on beta site
COPY asset_catalog/ /usr/share/nginx/html/asset_catalog/

# Copy custom Nginx configuration
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

HEALTHCHECK --interval=10s --timeout=3s --start-period=3s --retries=3 \
    CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1

CMD ["nginx", "-g", "daemon off;"]
