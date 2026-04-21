# cs214-proj4
**CS 214 Spring 2026 - Project IV: A Simple Chat Server**

A multi-threaded chat server implementation in C that supports multiple concurrent clients, private messaging, user status management, and room-based communication.

**Authors:** Haaris Toor (hbt20), Hassan Ibrahim (hi125)

## Quick Start

```bash
# Build the server and test client
make

# Run the server on port 8080
./chatd 8080

# In another terminal, connect as a client
./test_client localhost 8080 YourName
```

## Features

- ✅ Multi-client support with threading
- ✅ Unique screen name authentication
- ✅ Room-based messaging (`#all`)
- ✅ Private messaging between users
- ✅ User status management
- ✅ Protocol-based message validation
- ✅ Comprehensive error handling

## Protocol

Custom text-based protocol with message format: `VERSION|CODE|LENGTH|BODY|`

Message types: `NAM`, `SET`, `MSG`, `WHO`, `ERR`

## Documentation

📄 See **[README](README)** for complete implementation guide, test plan, and detailed specifications.

## Project Structure

```
.
├── chatd.c          # Main server implementation
├── protocol.c       # Protocol parsing & validation
├── protocol.h       # Protocol definitions
├── test_client.c    # Example test client
├── Makefile         # Build configuration
└── test.sh          # Testing script
```

## Development Status

🚧 **Skeleton implementation** - Contains detailed TODO comments for:
- Message parsing
- Client handlers (NAM, MSG, SET, WHO)
- Error handling
- Protocol validation

See inline TODO comments in source files for implementation guidance.

