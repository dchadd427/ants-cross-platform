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

# Configure and compile using default Makefiles
RUN emcmake cmake -B build_web \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

RUN cmake --build build_web -j$(nproc)

# =============================================================================
# Stage 2: High-Performance Lightweight Nginx Web Server
# =============================================================================
FROM nginx:alpine AS runner

# Clear default static files
RUN rm -rf /usr/share/nginx/html/*

# Copy compiled WebAssembly artifacts from builder stage
COPY --from=builder /src/build_web/src/ants_app/index.html /usr/share/nginx/html/
COPY --from=builder /src/build_web/src/ants_app/index.js /usr/share/nginx/html/
COPY --from=builder /src/build_web/src/ants_app/index.wasm /usr/share/nginx/html/
COPY --from=builder /src/build_web/src/ants_app/index.data /usr/share/nginx/html/

# Copy custom Nginx configuration
COPY docker/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

HEALTHCHECK --interval=10s --timeout=3s --start-period=3s --retries=3 \
    CMD wget -q --spider http://localhost/ || exit 1

CMD ["nginx", "-g", "daemon off;"]
