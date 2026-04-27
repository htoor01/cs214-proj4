# CS 214 Project IV — Implementation Decisions & History

Authors: Haaris Toor (hbt20), Hassan Ibrahim (hi125)
Due: 2026-05-01 11:59 PM

This file documents every implementation decision made, every bug encountered,
why previous approaches failed, and what the final solution is. Load this file
at the start of any future session to immediately understand the project.

---

## Project Overview

`chatd` is a multithreaded TCP chat server in C. One thread per client.
Clients communicate using a custom pipe-delimited protocol.

Protocol format: `VERSION|CODE|LENGTH|BODY`
- VERSION = "1" (literal, checked on every message)
- CODE = NAM / SET / MSG / WHO / ERR (exactly 3 chars)
- LENGTH = byte count of BODY including the trailing `|`
- BODY = pipe-delimited fields, always ending with `|`

Message types:
```
NAM  client→server  body: screen_name|
SET  client→server  body: status|
MSG  client→server  body: |recipient|text|        (empty sender = leading |)
     server→client  body: sender|recipient|text|
WHO  client→server  body: target|  (target = name or #all)
ERR  server→client  body: code|explanation|
```

Error codes: 0=fatal(close conn), 1=name in use, 2=unknown recipient,
3=illegal char, 4=too long.

Files:
- `chatd.c`      — server (main, handle_client, all handlers, broadcast)
- `protocol.c`   — parse_message, format_message, create_* helpers
- `protocol.h`   — constants, message_t struct, function prototypes
- `Makefile`     — gcc -Wall -Wextra -Werror -std=c99 -pthread -g
- `test.sh`      — automated 10-test bash script using nc
- `test_client.c`— interactive client for manual testing

---

## Bug 1: No Per-Client Receive Buffer (message framing)

### What the skeleton did
```c
char buffer[4096];
ssize_t bytes = recv(client->socket, buffer, sizeof(buffer)-1, 0);
buffer[bytes] = '\0';
parse_message(buffer, &msg);
```
One `recv()`, process one message, loop.

### Why it failed
TCP is a byte stream. A single `recv()` can return:
- Less than one message (partial read — rare but real)
- Exactly one message (lucky case)
- Multiple messages concatenated (common when client sends NAM then SET
  back-to-back — OS buffers both and delivers in one segment)

Tests 4 (SET after NAM) and 5 (WHO after NAM) always failed because the
server read `1|NAM|4|Bob|1|SET|10|Chatting!|` as one blob, parsed only
the NAM, and discarded the rest.

### The fix
Per-client heap-allocated accumulation buffer. The key loop structure:

```c
#define CLIENT_BUF_SIZE (MAX_MSG_LENGTH + 200)  // ~100 KB

char *buf = malloc(CLIENT_BUF_SIZE);
int fill = 0;

while (running) {
    ssize_t bytes = recv(client->socket, buf + fill, CLIENT_BUF_SIZE - fill - 1, 0);
    if (bytes <= 0) break;
    fill += (int)bytes;

    int processed;
    do {
        processed = 0;
        // 1. Find 3 pipes to locate end of header
        // 2. Parse LENGTH from header
        // 3. Check: do we have fill >= header_end + body_length bytes?
        // 4. If yes: parse full message, route it, memmove remaining bytes
        //            to front of buffer, decrement fill, set processed=1
        // 5. If no: break inner loop, go back to recv()
    } while (processed);
}
```

The `do { } while (processed)` inner loop is critical: after consuming one
message, there may be another complete message already in the buffer (e.g.,
if 3 messages arrived in one recv). Without it, you'd block on recv() waiting
for more data when you already have a complete message buffered.

---

## Bug 2: strtok Breaks on Empty MSG Sender Field

### What the skeleton did
```c
// In handle_msg_message():
char *copy = strdup(body);
char *sender    = strtok(copy, "|");
char *recipient = strtok(NULL, "|");
char *text      = strtok(NULL, "|");
```

### Why it failed
Client sends MSG body as `|recipient|text|` — empty sender (leading `|`).
`strtok` skips consecutive delimiters. So with body `|#all|Hello!|`:
- `strtok(copy, "|")` skips the leading `|`, returns `"#all"` (the recipient)
- `strtok(NULL, "|")` returns `"Hello!"` (the text)
- `strtok(NULL, "|")` returns NULL

Result: sender="the recipient", recipient="the text", text=NULL. All MSG
handling silently failed. This broke tests 7, 8, and 9.

### The fix
Replace strtok with manual strchr-based parsing:

```c
// body = "|recipient|text|" or "|#all|text|"
const char *p = body;

// sender field: everything up to first '|' (empty string here)
const char *sender_end = strchr(p, '|');
// sender is p..sender_end (length 0 for empty sender — ignored anyway)

// recipient: sender_end+1 up to next '|'
const char *recip_start = sender_end + 1;
const char *recip_end   = strchr(recip_start, '|');
// copy recipient: recip_start .. recip_end

// text: recip_end+1 up to LAST '|' (not first — text may contain '|')
const char *text_start = recip_end + 1;
const char *text_end   = strrchr(text_start, '|');  // ← strrchr, not strchr
// copy text: text_start .. text_end
```

The `strrchr` for text_end is important: the spec says message text may
include vertical bar characters. `strchr` would truncate `"Hi | there"` to
`"Hi "`. `strrchr` correctly finds the last `|` (the terminating one).

---

## Bug 3: TOCTOU Race on Duplicate Name Check

### What the skeleton did
```c
// handle_nam_message():
if (find_client_by_name(name)) {
    send_error(ERR_NAME_IN_USE);
    return;
}
// ... time gap here — another thread could insert same name ...
add_client(client);  // two clients with same name now in list
```
Check-then-act with the mutex released between the two operations.

### The fix
Atomic `try_add_client()` — holds the mutex for the entire check+insert:

```c
static int try_add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    int found = 0;
    for (client_t *c = clients; c; c = c->next) {
        if (c->authenticated && strcmp(c->screen_name, client->screen_name) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        client->next = clients;
        clients = client;
    }
    pthread_mutex_unlock(&clients_mutex);
    return !found;  // 1 = success, 0 = duplicate
}
```

---

## Bug 4: Use-After-Free in MSG and WHO Handlers

### What the skeleton did
```c
client_t *target = find_client_by_name(recipient);
// mutex released inside find_client_by_name
// ... another thread frees target here ...
send(target->socket, msg, len, 0);  // use-after-free
```
`find_client_by_name` returned a pointer after releasing the mutex.
Any concurrent disconnect could free the target struct before we used it.

### The fix
Never hold a pointer to another client's struct across a mutex boundary.
Instead, snapshot only the primitive you need (the socket fd for MSG,
the status string for WHO) while still holding the mutex:

```c
// For MSG private:
pthread_mutex_lock(&clients_mutex);
int target_fd = -1;
for (client_t *c = clients; c; c = c->next) {
    if (c->authenticated && strcmp(c->screen_name, recipient) == 0) {
        target_fd = c->socket;
        break;
    }
}
pthread_mutex_unlock(&clients_mutex);
if (target_fd == -1) { /* send ERR 2 */ return; }
send(target_fd, msg, strlen(msg), 0);

// For WHO single user:
char status_copy[MAX_STATUS + 1];
int found = 0;
pthread_mutex_lock(&clients_mutex);
for (client_t *c = clients; c; c = c->next) {
    if (c->authenticated && strcmp(c->screen_name, target) == 0) {
        strncpy(status_copy, c->status, MAX_STATUS);
        status_copy[MAX_STATUS] = '\0';
        found = 1;
        break;
    }
}
pthread_mutex_unlock(&clients_mutex);
```

Socket fds are safe to use after the mutex — the fd number stays valid
as long as the connection is open, and a failed send just returns EPIPE.

---

## Bug 5: Re-NAM Adds Client to List Twice

### What happened
If an authenticated client sent a second NAM message:
1. `handle_nam_message` updated `client->screen_name` to the new name
2. `try_add_client` walked the list — the old entry now had the NEW name
   (same struct, pointer aliasing), so no duplicate was detected
3. The same struct was inserted a second time as a new head

Result: the client appeared twice in the list (same pointer), causing
double-free on disconnect and corrupted broadcasts.

### The fix
One-line guard at the top of `handle_nam_message`:

```c
if (client->authenticated) return;  // ignore re-NAM
```

---

## Bug 6: broadcast_message Held Mutex During send()

### What happened
Original broadcast locked the mutex, iterated the list, and called `send()`
on each client while still holding the lock. If a client's send buffer was
full (blocking send) or the send took time, the entire server was locked —
no other thread could acquire the mutex, effectively serializing everything.

Worse: if the sending thread itself needed the mutex (e.g., a recursive
broadcast scenario), it would deadlock.

### The fix
Snapshot the socket fds into a local array under the lock, release the lock,
then send to each fd outside the lock:

```c
void broadcast_message(const char *message, client_t *exclude) {
    int sockets[1024];
    int count = 0;
    pthread_mutex_lock(&clients_mutex);
    for (client_t *c = clients; c && count < 1024; c = c->next) {
        if (c->authenticated && c != exclude)
            sockets[count++] = c->socket;
    }
    pthread_mutex_unlock(&clients_mutex);

    size_t len = strlen(message);
    for (int i = 0; i < count; i++)
        send(sockets[i], message, len, 0);
}
```

---

## Bug 7: Server Crashed on Dead Client (SIGPIPE)

### What happened
When a client disconnected abruptly, the next `send()` to that fd raised
SIGPIPE. Default SIGPIPE disposition is process termination — the server died.

### The fix
One line in `main()` before any threads start:

```c
signal(SIGPIPE, SIG_IGN);
```

With SIGPIPE ignored, `send()` returns -1 with errno=EPIPE instead of
killing the process. We don't check the return value of send (intentional —
a failed send to a dead client is not an error we need to handle).

---

## Bug 8: WHO #all Used O(n²) strcat

### What happened
```c
char response[WHO_BUF_SIZE];
response[0] = '\0';
// inside mutex loop:
strcat(response, name);   // O(n) scan each call
strcat(response, ": ");
strcat(response, status);
strcat(response, "\n");
```
`strcat` scans the full string to find the null terminator on every call.
With many users this is O(n²) total, and with no bounds checking it could
overflow WHO_BUF_SIZE.

### The fix
Track an offset, use `snprintf` with remaining space:

```c
int off = 0;
// inside mutex loop:
int n = snprintf(response + off, WHO_BUF_SIZE - off, "%s: %s\n", name, status);
if (n > 0 && off + n < WHO_BUF_SIZE) off += n;
```
O(n) total, bounds-safe.

---

## Bug 9: 100KB Stack Frames in protocol.c

### What happened
```c
char body[MAX_MSG_LENGTH];  // 99,999 bytes — on the stack
snprintf(body, sizeof(body), "%s|%s|%s|", sender, recipient, text);
```
Every call to `create_msg_message` or `create_err_message` pushed ~100KB
onto the stack. In a multithreaded server each thread has a fixed stack
(typically 8MB on Linux). With many simultaneous clients this wastes
enormous stack space and risks overflow.

### The fix
Heap-allocate sized exactly to the content:

```c
size_t body_size = strlen(sender) + strlen(recipient) + strlen(text) + 4;
char *body = malloc(body_size);
if (!body) return NULL;
snprintf(body, body_size, "%s|%s|%s|", sender, recipient, text);
char *result = format_message(MSG_MSG, body);
free(body);
return result;
```

---

## Bug 10: test.sh — Newline Between Messages Broke Protocol

### What the original test.sh did
```bash
RESPONSE=$(echo -e "1|NAM|4|Bob|\n1|SET|10|Chatting!|" | nc localhost $PORT)
```

### Why it failed
`echo -e` outputs a literal `\n` byte (0x0A) between the two messages.
The server's accumulation buffer saw: `1|NAM|4|Bob|\n1|SET|10|Chatting!|`
After consuming the NAM message, the remaining data was `\n1|SET|10|...`.
When the server tried to parse the next message, it checked if version == "1"
but found "\n1" (newline + 1). Version check failed → ERR 0 → connection closed.

### The fix
`printf` with no separator. The LENGTH field makes messages self-framing —
no delimiter between them is needed or wanted:

```bash
RESPONSE=$(printf '1|NAM|4|Bob|1|SET|10|Chatting!|' | nc localhost $PORT)
```

---

## Bug 11: test.sh — Wrong Body Lengths

### The confusion
Body = everything after the 3rd `|` (the separator after LENGTH), including
the trailing `|`. For `1|MSG|N||#all|Hello everyone!|`:
- The 3 header pipes are after "1", "MSG", and "N"
- Body starts at the `|` immediately after N, so body = `|#all|Hello everyone!|`
- Body length = 1 + 4 + 1 + 15 + 1 = 22

Original test.sh had wrong counts:
- Carol's SET: `"Online!|"` = 8 bytes (test had 7)
- Frank's MSG: `"|#all|Hello everyone!|"` = 22 bytes (test had 21)
- Ivy's MSG:   `"|NoSuchUser|Hello?|"` = 19 bytes (test had 27)

All fixed to correct values.

---

## Bug 12: test.sh Test 10 — No ERR Ever Sent

### What test 10 originally did
```bash
RESPONSE=$(echo "INVALID MESSAGE" | nc localhost $PORT)
```

### Why it failed
"INVALID MESSAGE" has no `|` characters. The server's header parser scans for
3 pipes to find the end of the header. It never found 3 pipes → never had a
complete parseable header → never called `parse_message` → never sent ERR 0.
The nc connection just closed on EOF, response was empty.

### The fix
Send a properly framed message with an invalid VERSION field:
```bash
RESPONSE=$(printf '0|NAM|4|Bob|' | nc localhost $PORT 2>/dev/null)
```
This has 3 pipes so the header parser finds a complete message. `parse_message`
checks version == "1", sees "0", returns -1 → server sends ERR 0.

The grep pattern was also wrong. The actual response is `1|ERR|25|0|...|`
so we match: `grep -qE "ERR\|[0-9]+\|0\|"` (not `grep -q "ERR|0"`).

---

## Bug 13: test_client.c — %ld for size_t (Linux GCC build failure)

### What happened
```c
snprintf(message, sizeof(message), "1|MSG|%ld|%s", strlen(body), body);
```
`strlen()` returns `size_t` (unsigned). `%ld` expects `long int` (signed).
On macOS (Apple clang 17), this compiled without warning. On Linux GCC with
`-Wall -Wextra -Werror` (which includes `-Wformat`), this is a fatal error:
`format '%ld' expects type 'long int' but argument is 'size_t'`

### The fix
All 4 occurrences (lines 123, 133, 140, 146): `%ld` → `%zu`
`%zu` is the correct C99 format specifier for `size_t`.

---

## Things That Were NOT Bugs (critic false positives)

### "Recoverable errors should close the connection"
Wrong. The spec explicitly says only ERR 0 is fatal. ERR 1-4 are recoverable;
the client may continue. Our handlers return normally after sending ERR 1-4.

### "SET broadcast sender should be the user's name, not #all"
Wrong. The spec's own example shows: `1|MSG|40|#all|#all|Bob is now "..."`
Both sender and recipient are `#all` for SET broadcasts. This is correct.

---

## Final Test Results

All 10 tests in test.sh pass. Test descriptions are in README.

Test 1:  NAM authentication             ✅
Test 2:  Duplicate name (ERR 1)         ✅
Test 3:  Invalid screen name (ERR 3)    ✅
Test 4:  SET status (multi-message)     ✅
Test 5:  WHO single user                ✅
Test 6:  WHO #all                       ✅
Test 7:  MSG broadcast to #all          ✅
Test 8:  MSG private message            ✅
Test 9:  MSG unknown recipient (ERR 2)  ✅
Test 10: Invalid protocol (ERR 0)       ✅

---

## Submission

```bash
cd /Users/hassan/PycharmProjects/PythonProject
mkdir -p P4
cp cs214-proj4/{chatd.c,protocol.c,protocol.h,Makefile,AUTHOR,README,test.sh,test_client.c} P4/
tar -czf P4.tar.gz P4/
```

Submit `P4.tar.gz`. AUTHOR contains: `hbt20 hi125`
