#!/bin/bash
# test.sh - Basic test script for chatd
# CS 214 Spring 2026 - Project IV
# Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)

PORT=8888
SERVER_PID=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

function print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

function print_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

function print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

function cleanup() {
    if [ ! -z "$SERVER_PID" ]; then
        echo "Stopping server (PID: $SERVER_PID)..."
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi
}

trap cleanup EXIT

# Build the project
print_test "Building project..."
make clean
make

if [ ! -f "./chatd" ]; then
    print_fail "chatd executable not found"
    exit 1
fi

print_pass "Build successful"

# Start the server
print_test "Starting server on port $PORT..."
./chatd $PORT &
SERVER_PID=$!

sleep 1

if ! ps -p $SERVER_PID > /dev/null; then
    print_fail "Server failed to start"
    exit 1
fi

print_pass "Server started (PID: $SERVER_PID)"

# TODO: Add automated tests here
# Examples:
# - Test connection
# - Test NAM message
# - Test duplicate names
# - Test message broadcasting
# - Test private messages
# - Test WHO queries
# - Test error conditions

print_test "Running basic connection test..."
# You can use netcat or your test_client here
# Example: echo "1|NAM|4|Bob|" | nc localhost $PORT

print_test "Manual testing mode"
echo "Server is running on port $PORT"
echo "You can now test with: ./test_client localhost $PORT <name>"
echo "Press Ctrl+C to stop the server"

wait $SERVER_PID
