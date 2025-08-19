# Simple Chat in C++

A lightweight client-server chat application written in modern C++ (C++17). 
This application was written as a way to learn about socket programming, multithreading, and simple command handling for a real-time text-based chat.

## Features

- **Client/Server Architecture**: Run a server and connect multiple clients.
- **Usernames**: Clients choose a username when joining.
- **Message Broadcasting**: Messages are sent to all connected users.
- **Private Messaging**: `/whisper <user> <msg>` sends a direct message to a user.
- **Command System**:
  - `/help` – List all available commands.
  - `/who` – See all connected users.
  - `/name <new_username>` – Change your username.
  - `/clear` – Clear your terminal.
  - `/ping` – Check connectivity with the server.
  - `/quit` – Disconnect and exit.
- **Graceful Disconnects**: Users leaving are announced to the room.
- **Signal Handling**: The server can be safely stopped with Ctrl+C.
- **Thread Safety**: All shared resources are protected with mutexes.

## Project Structure

- `server.cpp` – The main server implementation.
- `client.cpp` – The client application.
- `commands.cpp` / `commands.h` – Shared command logic and helpers.
- `Makefile` – Build script.

## Build Instructions

You must have `clang++` or `g++` installed with C++17 support.

```bash
make
```

This will build two binaries in the `bin/` directory:

- `bin/server`
- `bin/client`

## Running

### Start the Server

```bash
./bin/server
```

The server listens on port `5000` by default.

### Start a Client

```bash
./bin/client
```

You will be prompted to enter a username, then connected to the server.

## Notes

- The application currently works on a **local network (LAN)**.
- To make it available across the internet, port forwarding or tunneling would be required (Likely to be included in V.2 of this app).
- Maximum message length is limited to **1024 characters**.

## License

This project is released under the MIT License.
