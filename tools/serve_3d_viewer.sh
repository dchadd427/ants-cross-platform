#!/usr/bin/env bash
# =============================================================================
# Launch Local 3D Realistic Ants Viewer Server
# =============================================================================
set -e

PORT=${1:-8080}
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VIEWER_DIR="${ROOT_DIR}/web"

echo "=== Ants! Realistic 3D Title Scene Web Viewer ==="
echo "Serving from: ${VIEWER_DIR}"
echo "Opening URL: http://localhost:${PORT}/viewer3d/"

# Open browser on macOS
if command -v open >/dev/null 2>&1; then
    (sleep 1 && open "http://localhost:${PORT}/viewer3d/") &
fi

# Run lightweight HTTP server
cd "${VIEWER_DIR}"
python3 -m http.server "${PORT}"
