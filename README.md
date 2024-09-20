# imagenet
Overview
-
This project consists of two programs: `uqimageproc` (a server) and `uqimageclient` (a client). These programs are designed to interact over HTTP, using TCP, to process images based on requests made by clients. The server supports multiple clients and can perform operations like rotating, scaling, or flipping images.

This project employs and demonstrates the use of several key programming principles, including socket-based network programming, multithreaded programming, HTTP communication protocols, inter-process communication, and memory management. Additionally, it involves handling signals, concurrency control with mutexes, and the use of third-party libraries like FreeImage for image processing.

### `uqimageproc`
`uqimageproc` is a multithreaded server that listens for incoming client connections, processes image manipulation requests, and returns the modified images. It supports advanced functionalities such as connection limiting, signal handling (e.g., reporting statistics), and multistep image operations.

### `uqimageclient`
`uqimageclient` is a command-line tool that acts as a client, connecting to the uqimageproc server. It allows users to send images to the server, request specific image operations, and save the manipulated images locally.

Features
-
`uqimageproc:`
- Image Processing: Supports rotating, scaling, and flipping images.
- Multithreading: Handles multiple client connections concurrently.
- Signal Handling: Provides statistics on client connections and image operations using SIGHUP.
- Connection Limiting: (Optional) Limits the number of simultaneous client connections.
- HTTP Communication: Receives requests via HTTP and sends responses accordingly.
- Memory Management: Ensures no memory leaks during runtime.

`uqimageclient`:
- Command-Line Interface: Provides a simple interface to send images for processing.
- Image Manipulation Options: Allows users to rotate, scale, or flip images.
- HTTP Requests: Sends HTTP requests to the server and handles responses.
- File Handling: Reads input image files and writes the manipulated output to specified files.

Command Line Usage
-
`uqimageclient`
```C
./uqimageclient portno [--in infile] [--output outputfile] [--rotate degrees | --scale x y | --flip direction]
```
- portno: The port number the server is listening on.
- --in infile: Input image file (if omitted, stdin is used).
- --output outputfile: Output file for the manipulated image (if omitted, stdout is used).
- --rotate degrees: Rotates the image counter-clockwise by the specified degrees.
- --scale x y: Scales the image to the specified width (x) and height (y).
- --flip direction: Flips the image either horizontally (h) or vertically (v).

`uqimageproc`
```C
./uqimageproc [--listenOn portnum] [--max num]
```
- --listenOn portnum: Port number to listen on (defaults to ephemeral if omitted).
- --max num: Maximum number of client connections allowed (optional).
