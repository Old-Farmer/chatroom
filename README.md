# Chatroom

A simple chatroom written in C.

## Build

This project uses CMake build system.

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Run

```bash
# server
./server <ip> <port>

 # client
./client <ip> <port> <name>
```

## Design

Clients talks to each others through a server.

One Client will have one tcp connection to the server. This connection is used to both send and recv msgs.
The server is used to transfer user msgs.

The msg protocol is simple:

|name:char[]|'\0'|sentences:char[]|'\0'|

Client: ctrl-d to quit the client, Backspace to delete a character. ASCII support only.
