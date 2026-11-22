"""The container's health check: the converter answers GET /health on its own port (the image has no curl)."""
import http.client
import os
import sys

try:
    connection = http.client.HTTPConnection("127.0.0.1", int(os.environ.get("ANTS_IMG_PORT", "4050")), timeout=2)
    connection.request("GET", "/health")
    sys.exit(0 if connection.getresponse().status == 200 else 1)
except Exception:
    sys.exit(1)
