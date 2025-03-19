# Network Systems

This repository contains two advanced networking projects implemented in C:

1. **Reliable UDP File Transfer System** - A custom implementation of file transfer over UDP with reliability mechanisms
2. **High-Performance HTTP Web Server** - A multithreaded HTTP server supporting persistent connections

## 1. Reliable UDP File Transfer System

A robust implementation of a reliable file transfer protocol over UDP, similar to FTP but with custom reliability mechanisms to overcome the inherent unreliability of UDP.

### Features

- **Reliable Delivery**: Implements acknowledgment tracking, sequence numbering, and automatic retry mechanisms
- **Large File Support**: Successfully tested with files up to 5GB
- **FTP-like Operations**: Supports get, put, delete, and directory listing operations
- **Error Handling**: Comprehensive error detection and recovery

### Implementation Details

- Custom packet sequencing for ordered delivery
- Timeout-based retransmission for lost packets
- Flow control to prevent overwhelming the receiver
- Client-server architecture with clearly defined communication protocol

### Usage

**Server:**
```bash
$ gcc uftp_server.c -o server
$ ./server 5001  # Starts the server on port 5001
```

**Client:**
```bash
$ gcc uftp_client.c -o client
$ ./client 192.168.1.101 5001  # Connect to server at given IP and port
```

Client supports the following commands:
- `get [file_name]`: Download a file from the server
- `put [file_name]`: Upload a file to the server
- `delete [file_name]`: Delete a file on the server
- `ls`: List files on the server
- `exit`: Close the connection

## 2. High-Performance HTTP Web Server

A multithreaded HTTP/1.1 server implementation in C with support for persistent connections and request pipelining.

### Features

- **Multithreaded Architecture**: Handles up to 100 concurrent client connections
- **Persistent Connections**: Implements HTTP keep-alive with 10-second timeout
- **Request Pipelining**: Supports up to 1000 sequential requests per connection
- **Content Type Support**: Handles multiple MIME types including HTML, CSS, JS, and various image formats
- **Directory Listing**: Automatic directory content display when no index file is present

### Implementation Details

- Thread pool with mutex synchronization for efficient resource management
- Proper HTTP status code handling (200, 400, 403, 404, 405, 505)
- Signal handling for graceful server shutdown
- Non-blocking I/O with select() for timeout management

### Usage

```bash
$ gcc http_server.c -o server -pthread
$ ./server 8080  # Start server on port 8080
```

Server documents should be placed in the `./www` directory relative to where the server is run.

## Requirements

- C compiler (gcc recommended)
- POSIX-compliant system (Linux, macOS)
- pthread library

## Installation

Clone the repository:
```bash
git clone https://github.com/yourusername/network-protocol-engineering.git
cd network-protocol-engineering
```

Compile the projects:
```bash
# For UDP File Transfer
cd udp
make

# For HTTP Server
cd ../http
make
```

## Technical Details

Both projects demonstrate advanced network programming concepts including:

- Socket programming in C
- Protocol design and implementation
- Concurrency and thread management
- Error handling and recovery strategies
- Performance optimization techniques

## License

[MIT License](LICENSE)
