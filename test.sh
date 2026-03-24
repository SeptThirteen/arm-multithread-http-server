#!/usr/bin/env bash
# Functional test script for the ARM multi-thread HTTP server.
# Usage: ./test.sh [port]
set -euo pipefail

PORT=${1:-8080}
BASE="http://127.0.0.1:${PORT}"
PASS=0
FAIL=0

check() {
    local desc="$1"
    local expected="$2"
    local actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "  PASS  $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL  $desc  (expected=$expected, got=$actual)"
        FAIL=$((FAIL + 1))
    fi
}

# ── Build ────────────────────────────────────────────────────────────────────
echo "==> Building..."
make -s clean && make -s

# ── Start server ─────────────────────────────────────────────────────────────
echo "==> Starting server on port ${PORT}..."
rm -f users.db
./server "${PORT}" 4 &
SERVER_PID=$!
sleep 1

# Ensure server is up
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "ERROR: server failed to start"
    exit 1
fi

echo "==> Running tests..."

# GET /  → 200
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "${BASE}/")
check "GET /  returns 200" "200" "$STATUS"

# GET /register.html  → 200
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "${BASE}/register.html")
check "GET /register.html returns 200" "200" "$STATUS"

# GET /login.html  → 200
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "${BASE}/login.html")
check "GET /login.html returns 200" "200" "$STATUS"

# GET /notfound.html  → 404
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "${BASE}/notfound.html")
check "GET /notfound.html returns 404" "404" "$STATUS"

# POST /api/register new user  → 201
BODY=$(curl -s -X POST "${BASE}/api/register" \
     -d "username=alice&password=secret123" \
     -H "Content-Type: application/x-www-form-urlencoded")
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "${BASE}/api/register" \
     -d "username=bob&password=pass456" \
     -H "Content-Type: application/x-www-form-urlencoded")
check "POST /api/register new user returns 201" "201" "$STATUS"

# POST /api/register duplicate  → 409
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "${BASE}/api/register" \
     -d "username=alice&password=secret123" \
     -H "Content-Type: application/x-www-form-urlencoded")
check "POST /api/register duplicate returns 409" "409" "$STATUS"

# POST /api/login valid  → 200
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "${BASE}/api/login" \
     -d "username=alice&password=secret123" \
     -H "Content-Type: application/x-www-form-urlencoded")
check "POST /api/login valid credentials returns 200" "200" "$STATUS"

# POST /api/login wrong password  → 403
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X POST "${BASE}/api/login" \
     -d "username=alice&password=wrongpassword" \
     -H "Content-Type: application/x-www-form-urlencoded")
check "POST /api/login wrong password returns 403" "403" "$STATUS"

# DELETE method  → 405
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "${BASE}/")
check "DELETE / returns 405" "405" "$STATUS"

# Concurrent requests  → all 200
STATUSES=$(for i in 1 2 3 4 5; do
    curl -s -o /dev/null -w "%{http_code}\n" "${BASE}/" &
done; wait)
ALL_OK=true
while IFS= read -r s; do
    [ "$s" = "200" ] || ALL_OK=false
done <<< "$STATUSES"
if $ALL_OK; then
    echo "  PASS  5 concurrent GET / requests all return 200"
    PASS=$((PASS + 1))
else
    echo "  FAIL  5 concurrent GET / requests did not all return 200"
    FAIL=$((FAIL + 1))
fi

# ── Shutdown ─────────────────────────────────────────────────────────────────
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo ""
echo "==> Results: ${PASS} passed, ${FAIL} failed"
[ "${FAIL}" -eq 0 ]
