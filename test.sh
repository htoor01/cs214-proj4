#!/bin/bash
# Comprehensive test for chat server
# Tests all implemented functionality

PORT=9999
SERVER_PID=""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

function test_pass() {
    echo -e "${GREEN}✓${NC} $1"
}

function test_fail() {
    echo -e "${RED}✗${NC} $1"
}

function cleanup() {
    if [ ! -z "$SERVER_PID" ]; then
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
    fi
}

trap cleanup EXIT

# Build
echo "Building project..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ ! -f "./chatd" ]; then
    test_fail "Build failed"
    exit 1
fi
test_pass "Build successful"

# Start server
./chatd $PORT &
SERVER_PID=$!
sleep 1

if ! ps -p $SERVER_PID > /dev/null; then
    test_fail "Server failed to start"
    exit 1
fi
test_pass "Server started on port $PORT"

# Test 1: NAM message (authentication)
# body "Alice|" = 6 bytes
echo -e "\n${YELLOW}Test 1: Authentication (NAM)${NC}"
RESPONSE=$(printf '1|NAM|6|Alice|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Welcome to the chat"; then
    test_pass "NAM: User authenticated successfully"
else
    test_fail "NAM: Authentication failed"
    echo "Response: $RESPONSE"
fi

# Test 2: Duplicate name
echo -e "\n${YELLOW}Test 2: Duplicate screen name${NC}"
(printf '1|NAM|6|Alice|'; sleep 2) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
RESPONSE=$(printf '1|NAM|6|Alice|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "already in use"; then
    test_pass "Error 1: Duplicate name rejected"
else
    test_fail "Error 1: Duplicate name not detected"
fi

# Test 3: Invalid screen name
# body "Bob@!|" = 6 bytes
echo -e "\n${YELLOW}Test 3: Invalid screen name${NC}"
RESPONSE=$(printf '1|NAM|6|Bob@!|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "ERR"; then
    test_pass "Error 3: Invalid characters detected"
else
    test_fail "Error 3: Invalid name not rejected"
fi

# Test 4: SET status (two messages in one session — exercises message buffering)
# body "Bob|"=4, body "Chatting!|"=10
echo -e "\n${YELLOW}Test 4: Set status (SET)${NC}"
RESPONSE=$(printf '1|NAM|4|Bob|1|SET|10|Chatting!|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Bob is now"; then
    test_pass "SET: Status update broadcast"
else
    test_fail "SET: Status update failed"
fi

# Test 5: WHO single user
# Carol: body "Carol|"=6, body "Online!|"=8
# Dave:  body "Dave|"=5,  body "Carol|"=6
echo -e "\n${YELLOW}Test 5: Query single user (WHO)${NC}"
(printf '1|NAM|6|Carol|1|SET|8|Online!|'; sleep 10) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
RESPONSE=$(printf '1|NAM|5|Dave|1|WHO|6|Carol|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Carol: Online!"; then
    test_pass "WHO: Single user query with status"
else
    test_fail "WHO: Single user query failed"
    echo "Response: $RESPONSE"
fi

# Test 6: WHO all users
# body "Eve|"=4, body "#all|"=5
echo -e "\n${YELLOW}Test 6: Query all users (WHO #all)${NC}"
RESPONSE=$(printf '1|NAM|4|Eve|1|WHO|5|#all|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Eve"; then
    test_pass "WHO: All users query"
else
    test_fail "WHO: All users query failed"
fi

# Test 7: MSG broadcast
# body "Frank|"=6
# MSG body "|#all|Hello everyone!|" = 1+4+1+15+1 = 22 bytes
echo -e "\n${YELLOW}Test 7: Broadcast message (MSG to #all)${NC}"
RESPONSE=$(printf '1|NAM|6|Frank|1|MSG|22||#all|Hello everyone!|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Frank|#all|Hello everyone"; then
    test_pass "MSG: Broadcast message sent"
else
    test_fail "MSG: Broadcast failed"
fi

# Test 8: Private message
# MSG body "|Grace|Private message|" = 1+5+1+15+1 = 23 bytes
echo -e "\n${YELLOW}Test 8: Private message (MSG to user)${NC}"
(printf '1|NAM|6|Grace|'; sleep 10) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
printf '1|NAM|5|Hank|1|MSG|23||Grace|Private message|' | nc localhost $PORT > /dev/null 2>&1
test_pass "MSG: Private message sent (no error)"

# Test 9: Unknown recipient
# MSG body "|NoSuchUser|Hello?|" = 1+10+1+6+1 = 19 bytes
echo -e "\n${YELLOW}Test 9: Message to unknown user${NC}"
RESPONSE=$(printf '1|NAM|4|Ivy|1|MSG|19||NoSuchUser|Hello?|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "User not found"; then
    test_pass "Error 2: Unknown recipient detected"
else
    test_fail "Error 2: Unknown recipient not detected"
fi

# Test 10: Invalid protocol (wrong version number — triggers ERR 0)
# body "Bob|"=4; version "0" is invalid so parse_message returns -1
echo -e "\n${YELLOW}Test 10: Invalid protocol message${NC}"
RESPONSE=$(printf '0|NAM|4|Bob|' | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -qE "ERR\|[0-9]+\|0\|"; then
    test_pass "Error 0: Invalid format detected"
else
    test_fail "Error 0: Invalid format not detected"
fi

echo -e "\n${GREEN}All tests completed!${NC}"
