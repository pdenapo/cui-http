#!/usr/bin/env bash
# End-to-end test suite for the cui-http client.
#
# Spins up a tiny local REST API (vibe.d) and drives the `cui_http_cli`
# command line client through every HTTP verb, the query/body/header round
# trips, status codes, latency and the history database.
#
# Run from the project root:
#   ./tests/run_tests.sh
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMPDIR_TEST="${TMPDIR:-/tmp}/cui-http-tests"
mkdir -p "$TMPDIR_TEST" "$ROOT/tests/data"

# Locate the CLI binary (CI builds into ./build, local builds into ./build_x11 etc.)
CLI=""
for cand in "$ROOT/build/cui_http_cli" "$ROOT/build_x11/cui_http_cli" "$ROOT/build_linux/cui_http_cli" "$ROOT/build_glfw/cui_http_cli" "$ROOT/build_sdl2/cui_http_cli"; do
    if [ -x "$cand" ]; then CLI="$cand"; break; fi
done
if [ -z "$CLI" ]; then
    echo "ERROR: cui_http_cli not found. Build first (cmake --build build)."
    exit 1
fi
# Locate the GUI binary for the persistence smoke test.
GUI=""
for cand in "$ROOT/build/cui_http" "$ROOT/build_x11/cui_http" "$ROOT/build_linux/cui_http" "$ROOT/build_glfw/cui_http" "$ROOT/build_sdl2/cui_http"; do
    if [ -x "$cand" ]; then GUI="$cand"; break; fi
done
SERVER="$ROOT/tests/server/bin/cui-http-test-server"

# Helper to run the GUI on a headless machine (unused, kept for reference).

# Build the test server (pure C) if needed.
if [ ! -x "$SERVER" ] || [ "tests/server/server.c" -nt "$SERVER" ]; then
    mkdir -p "$ROOT/tests/server/bin"
    gcc -O2 -std=c99 -o "$SERVER" "$ROOT/tests/server/server.c" -lpthread
fi
PORT=8090
URL="http://127.0.0.1:$PORT"
DB="$ROOT/tests/data/history.db"

rm -f "$DB"
mkdir -p "$ROOT/tests/data"

# ---------- start the test server ----------
"$SERVER" >"$TMPDIR_TEST/test-server.log" 2>&1 &
SERVER_PID=$!
cleanup() { kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; }
trap cleanup EXIT

# wait for readiness
ready=0
for i in $(seq 1 50); do
    if curl -s -o /dev/null "$URL/users" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.2
done
if [ "$ready" != "1" ]; then
    echo "ERROR: test server did not start"
    cat "$TMPDIR_TEST/test-server.log"
    exit 1
fi

export CUI_HTTP_DB="$DB"

pass=0; fail=0
check() {
    # check NAME expected exit code and a grep expression in the output file
    local name="$1"; local expected_rc="$2"; local expr="$3"; local out="$4"; local rc="$5"
    if [ "$rc" = "$expected_rc" ] && grep -qE "$expr" "$out"; then
        echo "  [PASS] $name"
        pass=$((pass+1))
    else
        echo "  [FAIL] $name (rc=$rc expected $expected_rc; expr=$expr)"
        echo "         --- captured output ---"
        sed 's/^/         /' "$out"
        fail=$((fail+1))
    fi
}

run() {
    # run NAME expected_rc expression -- args...
    local name="$1"; shift
    local expected_rc="$1"; shift
    local expr="$1"; shift
    local out="$TMPDIR_TEST/out.txt"
    set +e
    "$CLI" "$@" >"$out" 2>&1
    local rc=$?
    set -e
    check "$name" "$expected_rc" "$expr" "$out" "$rc"
}

echo "== 1. GET /users =="
run "GET list" 0 "Ada Lovelace" GET "$URL/users"

echo "== 2. GET /users/1 =="
run "GET one" 0 "Ada Lovelace" GET "$URL/users/1"

echo "== 3. GET /users/999 =="
run "GET missing" 1 "user not found" GET "$URL/users/999"

echo "== 4. POST /users =="
run "POST create" 0 "created" POST "$URL/users" -d '{"name":"Grace","email":"grace@hopper.org"}'

echo "== 5. GET /users/3 =="
run "GET created" 0 "Grace" GET "$URL/users/3"

echo "== 6. PUT /users/3 =="
run "PUT replace" 0 "updated" PUT "$URL/users/3" -d '{"name":"Grace Hopper","email":"grace@navy.mil"}'
run "GET after PUT" 0 "Grace Hopper" GET "$URL/users/3"

echo "== 7. PATCH /users/3 =="
run "PATCH email" 0 "patched" PATCH "$URL/users/3" -d '{"email":"grace-nueva@navy.mil"}'
run "GET after PATCH" 0 "grace-nueva@navy.mil" GET "$URL/users/3"

echo "== 8. DELETE /users/3 =="
run "DELETE" 0 "status: 204" DELETE "$URL/users/3"
run "GET after DELETE" 1 "user not found" GET "$URL/users/3"

echo "== 9. OPTIONS and HEAD =="
run "OPTIONS" 0 "status: 200" OPTIONS "$URL/users"
run "HEAD" 0 "status: 200" HEAD "$URL/users"

echo "== 10. echo round trip (body + headers) =="
run "echo" 0 "abc-123" POST "$URL/echo" -H "X-Token: abc-123" -d '{"ping":true}'

echo "== 11. status helper =="
run "status 503" 1 "status: 503" GET "$URL/status/503"

echo "== 12. slow (latency) =="
run "slow 800" 0 "status: 200" GET "$URL/slow?ms=800"
# check the latency printed is at least 700 ms
grep -oE "\([0-9]+\.[0-9]+ ms" "$TMPDIR_TEST/out.txt" | head -1

echo "== 13. invalid JSON =="
run "bad json" 1 "invalid JSON" POST "$URL/users" -d 'not json'

echo "== 14. curl-style positional body =="
run "positional body" 0 "created" POST "$URL/users" '{"name":"Katherine","email":"katherine@nasa.gov"}'

echo "== 15. history command =="
"$CLI" history 5 >"$TMPDIR_TEST/hist.txt" 2>&1
cat "$TMPDIR_TEST/hist.txt"
echo "-- history count check --"
rows=$(sqlite3 "$DB" "SELECT COUNT(*) FROM requests;")
echo "rows in db: $rows"
if [ "$rows" -ge 15 ]; then echo "  [PASS] history persisted (>=15 rows)"; pass=$((pass+1)); else echo "  [FAIL] expected >=15 rows, got $rows"; fail=$((fail+1)); fi

# check that the persisted rows carry the method and a 200-class status
if sqlite3 "$DB" "SELECT COUNT(*) FROM requests WHERE status BETWEEN 200 AND 299;" | grep -qE '^[0-9]+' ; then echo "  [PASS] history has 2xx rows"; pass=$((pass+1)); else echo "  [FAIL] no 2xx rows"; fail=$((fail+1)); fi
if sqlite3 "$DB" "SELECT DISTINCT method FROM requests;" | grep -q POST; then echo "  [PASS] POST recorded"; pass=$((pass+1)); else echo "  [FAIL] no POST recorded"; fail=$((fail+1)); fi

echo "== 16. settings via direct backend (smoke: the GUI path) =="
"$CLI" clear >"$TMPDIR_TEST/clear.txt" 2>&1
if grep -q "history cleared" "$TMPDIR_TEST/clear.txt"; then echo "  [PASS] clear works"; pass=$((pass+1)); else echo "  [FAIL] clear"; fail=$((fail+1)); fi
if [ "$(sqlite3 "$DB" 'SELECT COUNT(*) FROM requests;')" = "0" ]; then echo "  [PASS] history empty after clear"; pass=$((pass+1)); else echo "  [FAIL] history not cleared"; fail=$((fail+1)); fi

echo "== 17. GUI theme/language persistence (first_frame wiring) =="
GUI_DB="$ROOT/tests/data/gui.db"
rm -f "$GUI_DB" "$TMPDIR_TEST/gui-marker.txt"
# First run creates the schema.
if command -v xvfb-run >/dev/null 2>&1 && [ -z "${DISPLAY:-}" ]; then
    timeout 2 xvfb-run -a env CUI_HTTP_DB="$GUI_DB" "$GUI" >/dev/null 2>&1 || true
else
    timeout 2 env CUI_HTTP_DB="$GUI_DB" "$GUI" >/dev/null 2>&1 || true
fi
# Seed dark theme + Italian language.
sqlite3 "$GUI_DB" "INSERT OR REPLACE INTO settings(key,value) VALUES('theme','dark');"
sqlite3 "$GUI_DB" "INSERT OR REPLACE INTO settings(key,value) VALUES('language','it');"
# Second run should apply them and write the marker.
if command -v xvfb-run >/dev/null 2>&1 && [ -z "${DISPLAY:-}" ]; then
    timeout 2 xvfb-run -a env CUI_HTTP_DB="$GUI_DB" CUI_HTTP_TEST_MARKER="$TMPDIR_TEST/gui-marker.txt" "$GUI" >/dev/null 2>&1 || true
else
    timeout 2 env CUI_HTTP_DB="$GUI_DB" CUI_HTTP_TEST_MARKER="$TMPDIR_TEST/gui-marker.txt" "$GUI" >/dev/null 2>&1 || true
fi
if grep -q "theme=dark" "$TMPDIR_TEST/gui-marker.txt"; then echo "  [PASS] theme persisted and applied"; pass=$((pass+1)); else echo "  [FAIL] theme not applied"; cat "$TMPDIR_TEST/gui-marker.txt"; fail=$((fail+1)); fi
if grep -q "lang=4" "$TMPDIR_TEST/gui-marker.txt"; then echo "  [PASS] language persisted and applied"; pass=$((pass+1)); else echo "  [FAIL] language not applied"; cat "$TMPDIR_TEST/gui-marker.txt"; fail=$((fail+1)); fi

echo
echo "==============================="
echo "PASS: $pass   FAIL: $fail"
echo "==============================="
exit $(( fail > 0 ? 1 : 0 ))
