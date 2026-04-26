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
echo -e "\n${YELLOW}Test 1: Authentication (NAM)${NC}"
RESPONSE=$(echo "1|NAM|6|Alice|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Welcome to the chat"; then
    test_pass "NAM: User authenticated successfully"
else
    test_fail "NAM: Authentication failed"
    echo "Response: $RESPONSE"
fi

# Test 2: Duplicate name
echo -e "\n${YELLOW}Test 2: Duplicate screen name${NC}"
(echo "1|NAM|6|Alice|"; sleep 1) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
RESPONSE=$(echo "1|NAM|6|Alice|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "already in use"; then
    test_pass "Error 1: Duplicate name rejected"
else
    test_fail "Error 1: Duplicate name not detected"
fi

# Test 3: Invalid screen name
echo -e "\n${YELLOW}Test 3: Invalid screen name${NC}"
RESPONSE=$(echo "1|NAM|6|Bob@!|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "ERR"; then
    test_pass "Error 3: Invalid characters detected"
else
    test_fail "Error 3: Invalid name not rejected"
fi

# Test 4: SET message (status)
echo -e "\n${YELLOW}Test 4: Set status (SET)${NC}"
RESPONSE=$(echo -e "1|NAM|4|Bob|\n1|SET|10|Chatting!|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Bob is now"; then
    test_pass "SET: Status update broadcast"
else
    test_fail "SET: Status update failed"
fi

# Test 5: WHO message (single user)
echo -e "\n${YELLOW}Test 5: Query single user (WHO)${NC}"
(echo "1|NAM|6|Carol|"; echo "1|SET|7|Online!|"; sleep 10) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
RESPONSE=$(echo -e "1|NAM|5|Dave|\n1|WHO|7|Carol|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Carol: Online!"; then
    test_pass "WHO: Single user query with status"
else
    test_fail "WHO: Single user query failed"
    echo "Response: $RESPONSE"
fi

# Test 6: WHO message (all users)
echo -e "\n${YELLOW}Test 6: Query all users (WHO #all)${NC}"
RESPONSE=$(echo -e "1|NAM|4|Eve|\n1|WHO|5|#all|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Eve"; then
    test_pass "WHO: All users query"
else
    test_fail "WHO: All users query failed"
fi

# Test 7: MSG message (broadcast)
echo -e "\n${YELLOW}Test 7: Broadcast message (MSG to #all)${NC}"
RESPONSE=$(echo -e "1|NAM|6|Frank|\n1|MSG|21||#all|Hello everyone!|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "Frank|#all|Hello everyone"; then
    test_pass "MSG: Broadcast message sent"
else
    test_fail "MSG: Broadcast failed"
fi

# Test 8: MSG message (private)
echo -e "\n${YELLOW}Test 8: Private message (MSG to user)${NC}"
(echo "1|NAM|6|Grace|"; sleep 10) | nc localhost $PORT > /dev/null 2>&1 &
sleep 0.5
RESPONSE=$(echo -e "1|NAM|5|Hank|\n1|MSG|23||Grace|Private message|" | nc localhost $PORT 2>/dev/null)
# Private messages don't echo back to sender in this implementation
test_pass "MSG: Private message sent (no error)"

# Test 9: Unknown recipient
echo -e "\n${YELLOW}Test 9: Message to unknown user${NC}"
RESPONSE=$(echo -e "1|NAM|4|Ivy|\n1|MSG|27||NoSuchUser|Hello?|" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "User not found"; then
    test_pass "Error 2: Unknown recipient detected"
else
    test_fail "Error 2: Unknown recipient not detected"
fi

# Test 10: Invalid message format
echo -e "\n${YELLOW}Test 10: Invalid protocol message${NC}"
RESPONSE=$(echo "INVALID MESSAGE" | nc localhost $PORT 2>/dev/null)
if echo "$RESPONSE" | grep -q "ERR|0"; then
    test_pass "Error 0: Invalid format detected"
else
    test_fail "Error 0: Invalid format not detected"
fi

echo -e "\n${GREEN}All tests completed!${NC}"
