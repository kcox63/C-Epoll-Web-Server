# C Epoll HTTP Web Server

An event-driven HTTP web server written in C using Linux epoll for scalable I/O and concurrent client handling.

## Overview

This project was developed as part of Systems Programming coursework at the University of Illinois Chicago. The server uses Linux's epoll API to efficiently manage multiple client connections through an event-driven architecture.

The application supports HTTP GET and POST requests, serves static files, processes client requests, and manages concurrent connections using non-blocking I/O techniques.

## Features

- Event-driven architecture using Linux epoll
- Concurrent client handling
- HTTP GET request support
- HTTP POST request support
- Static file serving
- Custom API endpoints
- Content-Length parsing
- Error handling for malformed requests
- Efficient file transmission using chunked sends
- Scalable I/O management

## Supported Endpoints

### GET /status

Returns a simple status response indicating that the server is running.

### GET /echo

Returns request header information.

### GET /saved

Returns previously stored data.

### POST /info

Stores request body data and returns the saved content.

### GET /filename

Serves static files from the server directory.

## Technologies Used

- C
- Linux
- POSIX Sockets
- epoll
- TCP/IP Networking
- HTTP Protocol

## Running the Project

### Compile

```bash
gcc webserver.c -o server
```

### Run

```bash
./server 8080
```

The server will start listening on port 8080.

## Example Requests

### Status Check

```bash
curl http://127.0.0.1:8080/status
```

### Save Data

```bash
curl -X POST http://127.0.0.1:8080/info -d "Hello World"
```

### Retrieve Saved Data

```bash
curl http://127.0.0.1:8080/saved
```

### Request a File

```bash
curl http://127.0.0.1:8080/example.txt
```

## Skills Demonstrated

- Systems Programming
- Linux Development
- Socket Programming
- Event-Driven Architecture
- Concurrent Programming
- HTTP Protocol Implementation
- Memory Management
- File I/O
- Network Programming
- Debugging and Testing

## Future Improvements

- Multithreaded request handling
- HTTPS support
- MIME type detection
- Persistent connections
- Request logging
- Configuration file support
- Directory browsing protection

## Author

Kevin Cox

Computer Science Student  
University of Illinois Chicago  
Software Engineering Concentration  
Chick Evans Scholar

