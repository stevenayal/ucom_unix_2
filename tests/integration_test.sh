#!/usr/bin/env bash
set -euo pipefail
PORT="${TP_TEST_PORT:-5656}"
SERVER_LOG="$(mktemp)"; CLIENT_LOG="$(mktemp)"
cleanup() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$SERVER_LOG" "$CLIENT_LOG"
}
trap cleanup EXIT
TP_USER=ucom TP_PASS=unix ./server "$PORT" >"$SERVER_LOG" 2>&1 & SERVER_PID=$!
for _ in {1..30}; do grep -q "Servidor escuchando" "$SERVER_LOG" && break; sleep 0.1; done
printf 'ucom\nunix\necho hola-ucom\ncd /tmp\npwd\nprintf "uno\\ndos\\n"\nuname -s\nexit\n' |
  timeout 10 ./client 127.0.0.1 "$PORT" >"$CLIENT_LOG" 2>&1
grep -q "Autenticacion correcta" "$CLIENT_LOG"
grep -q "hola-ucom" "$CLIENT_LOG"
grep -q "/tmp" "$CLIENT_LOG"
grep -q "uno" "$CLIENT_LOG"
grep -q "dos" "$CLIENT_LOG"
grep -q "Linux" "$CLIENT_LOG"
echo "OK: shell persistente y comunicacion bidireccional superadas"
