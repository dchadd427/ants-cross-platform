# =============================================================================
# The login helper of the owner's page (tools/admin_login.py, docs/MAP_SUBMISSIONS.md "The owner's page"): Sign in with Discord, then a signed cookie. One small python program, no packages.
#   docker build -f Dockerfile.login -t ants-admin-login .
# It is configured by its environment only (docker-compose.stack.yml lists the variables) and listens on 4040 inside the network of the stack; the site's nginx passes /admin/login, /admin/callback
# and /admin/logout to it. It keeps nothing: no file, no database.
# =============================================================================
FROM python:3.12-alpine
RUN adduser -S -H -s /sbin/nologin ants
COPY tools/admin_login.py /app/admin_login.py
USER ants
EXPOSE 4040
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD ["python3", "-c", "import socket; socket.create_connection(('127.0.0.1', 4040), 2).close()"]
ENTRYPOINT ["python3", "-I", "/app/admin_login.py"]
