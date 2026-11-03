"""`python -m hub.health`: exit 0 when the hub of this container answers on its port (the image's HEALTHCHECK; the image has no curl)."""
import http.client
import os
import sys


def main():
    port = int(os.environ.get("ANTS_HUB_PORT", "4040"))
    try:
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
        conn.request("GET", "/hub/healthz")
        ok = conn.getresponse().status == 200
    except (OSError, http.client.HTTPException, ValueError):
        ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
