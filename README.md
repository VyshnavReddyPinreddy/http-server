```markdown
# High-Performance HTTP Server in C

A lightweight, C-based HTTP/1.1 static file server built on POSIX sockets. This repository demonstrates the evolution of multi-client network server architectures—progressing from a basic **thread-per-connection** model to a **synchronized thread pool**, and finally to an event-driven **`epoll`** architecture for maximum concurrency.

---

## 📁 Repository Structure


```

.
├── server.c                  # High-performance event-driven HTTP server using Linux epoll
├── threadPool.c              # Multi-threaded HTTP server using a fixed worker thread pool
├── threadPerConnection.c     # Multi-threaded HTTP server spawning a new thread per request
├── Makefile                  # Build script for compiling and running the default server
├── www/                      # Web root directory containing sample static assets
│   ├── index.html            # Main page to test HTTP GET responses
│   ├── about.html            # Secondary HTML page for testing routing
│   ├── style.css             # CSS stylesheet for MIME type testing
│   ├── favicon.png           # Image asset
│   ├── logo.png              # Image asset
│   └── test.bin              # Binary file for testing large transfers & zero-copy sendfile()
└── .gitignore

```

---

## 🚀 Server Architecture Versions

1. **`threadPerConnection.c` (Thread-per-Connection)**
   * Spawns a dedicated POSIX thread (`pthread_create`) for every incoming client connection.
   * Simple to implement, but suffers from high resource overhead and limited scalability under heavy load due to thread creation costs and OS context switching.

2. **`threadPool.c` (Synchronized Thread Pool)**
   * Pre-allocates a fixed number of worker threads at startup.
   * Uses a thread-safe task queue protected by mutexes and condition variables to reuse threads, eliminating thread creation overhead per request.

3. **`server.c` (Event-Driven `epoll` — Main Server)**
   * Non-blocking I/O multiplexing driven by the Linux `epoll` system call.
   * A single event loop efficiently handles thousands of concurrent connections with minimal CPU and memory overhead.
   * Features zero-copy kernel-to-socket file transmission using `sendfile()`.

---

## 🛠️ Key Features

* **HTTP/1.1 Protocol Support:** Implements basic parsing for `GET` and `HEAD` requests.
* **MIME Type Handling:** Automatically sets Content-Type headers for `.html`, `.css`, `.png`, and binary files.
* **Keep-Alive & Timeout Control:** Supports persistent connections.
* **Zero-Copy Transfer:** Utilizes `sendfile()` to bypass user-space memory buffering during static file delivery.

---

## 🔨 Building & Running

### Prerequisites
* GCC compiler
* Linux environment (or WSL on Windows) with support for POSIX threads and `epoll`

### Using the Makefile

To compile and launch the main event-driven server (`server.c`):

```bash
make

```

To clean up compiled binaries:

```bash
make clean

```

### Compiling Individual Versions Manually

If you want to compile and test a specific architecture variant:

```bash
# Thread-per-Connection
gcc -Wall -Wextra -Wpedantic threadPerConnection.c -o s_thread_per_conn -pthread
./s_thread_per_conn

# Thread Pool
gcc -Wall -Wextra -Wpedantic threadPool.c -o s_thread_pool -pthread
./s_thread_pool

# Event-Driven Epoll Server
gcc -Wall -Wextra -Wpedantic server.c -o s -pthread
./s

```

---

## 🧪 Verification

Once the server is running, open your web browser or run `curl` commands to test serving files from the `www/` directory:

```bash
# Test main HTML page
curl -i http://localhost:8080/index.html

# Test image delivery
curl -i http://localhost:8080/logo.png

# Test HEAD request
curl -I http://localhost:8080/about.html

```
## 🗺️ Future Enhancements

* **HTTP Method Support:** Extend request parsing to support `POST`, `PUT`, `PATCH`, and `DELETE` methods alongside existing `GET` and `HEAD` handlers.
```

```
