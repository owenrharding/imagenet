// OWEN HARDING 48007618 CSSE2310 A4

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <netdb.h>
#include <unistd.h>
#include <csse2310a4.h>

////////////////////////////////////////////////////////////////////////////////

// Enum Definitions

// Exit status values.
typedef enum {
    SUCCESS = 0,
    USAGE_ERROR = 9,
    READ_ERROR = 2,
    WRITE_ERROR = 19,
    PORT_ERROR = 6,
    IMAGE_ERROR = 20,
    OUTPUT_ERROR = 1,
    RESPONSE_ERROR = 4,
    COMM_ERROR = 11
} ExitCodes;

// Operation types.
typedef enum { ROTATE, SCALE, FLIP, UNSET } Operation;

////////////////////////////////////////////////////////////////////////////////

// Structure Definitions

// Structure to hold the I/O of the program.
typedef struct {
    const char* inPath;
    const char* outPath;
    FILE* in;
    FILE* out;
} Io;

// Structure to hold x and y values of a scale argument.
typedef struct {
    int x;
    int y;
} Scale;

// Structure to hold program arguments - determined from command line.
typedef struct {
    const char* port;
    Io io;
    int rotate;
    Scale scale;
    char flip;
    Operation op;
} CommandLineArgs;

// Structure to hold the two file descriptors (read from and write to) that
// describe the server this program is connecting to.
typedef struct {
    FILE* to;
    FILE* from;
} Connection;

// Structure to hold the content and number of bytes in content read in from a
// file stream.
typedef struct {
    unsigned char* content;
    int nBytes;
} Data;

// Structure to hold the contents of a deconstructed HTTP response. Populated
// with the use of get_HTTP_response function from provided csse2310a4.h
// library.
typedef struct {
    int status;
    char* statusExplanation;
    unsigned char* body;
    unsigned long len;
    HttpHeader** headers;
} Response;

////////////////////////////////////////////////////////////////////////////////

// Default Values

#define INITIAL_BUFFER_SIZE 1024
#define ROT_MAX 360
#define ROT_MIN (-360)
#define BASE 10
#define SCALE_MAX 10000
#define OK 200
const char* const defaultHost = "localhost";
const char* const requestType = "POST";

////////////////////////////////////////////////////////////////////////////////

// String Constants

const char* const inArg = "--in";
const char* const outputArg = "--output";
const char* const rotateArg = "--rotate";
const char* const scaleArg = "--scale";
const char* const flipArg = "--flip";

const char* const rotateOp = "rotate";
const char* const scaleOp = "scale";
const char* const flipOp = "flip";
const char* const defaultOp = "rotate";
const int defaultArg = 0;

const char* const errorUsage
        = "Usage: uqimageclient portno [--in infile] [--output outputfilename] "
          "[--rotate degrees | --scale x y | --flip direction]\n";
const char* const errorRead
        = "uqimageclient: unable to open file \"%s\" for reading\n";
const char* const errorWrite
        = "uqimageclient: unable to open file \"%s\" for writing\n";
const char* const errorPort
        = "uqimageclient: unable to establish connection to port \"%s\"\n";
const char* const errorImage = "uqimageclient: no data in input image\n";
const char* const errorOutput = "uqimageclient: unable to write output\n";
const char* const errorComm = "uqimageclient: server communication error\n";

////////////////////////////////////////////////////////////////////////////////

// Function Prototypes

// Command Line Processing
CommandLineArgs process_command_line(int argc, char* argv[]);
void process_arg(CommandLineArgs* args, int* argc, char*** argv);
bool str_is_num(const char* str);
Io check_files(Io io);
Connection check_connect(const char* port);

// Runtime Behaviour
Data read_image(FILE* in);
void send_request(FILE* to, CommandLineArgs args, Data data);
Response get_response(FILE* from);
void handle_response(Response res, Io io);

// Error Messages
void throw_usage_error(void);
void throw_read_error(const char* filename);
void throw_write_error(const char* filename);
void throw_port_error(const char* portname);
void throw_image_error(void);
void throw_output_error(void);
void throw_response_error(void);
void throw_comm_error(void);

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    CommandLineArgs args = process_command_line(argc, argv);

    args.io = check_files(args.io);

    Connection cnxn = check_connect(args.port);

    Data imageData = read_image(args.io.in);

    send_request(cnxn.to, args, imageData);

    Response res = get_response(cnxn.from);

    handle_response(res, args.io);
}

// process_command_line()
//
// Parses command line, does an initial validation on the arguments, and if args
// are correct, extracts them and saves them to the returned CommandLineArgs
// struct.
// REF: Structure of this function is HEAVILY inspired by CSSE2310 Assignment 1
// REF: sample solution written and provided by Peter Sutton.
CommandLineArgs process_command_line(int argc, char* argv[])
{
    CommandLineArgs args
            = {0, {NULL, NULL, NULL, NULL}, 0, {0, 0}, '\0', UNSET};
    // Skip over program name.
    argc--;
    argv++;
    if (argc == 0) { // No arguments present other than program name. No portno.
        throw_usage_error();
    }
    if (strlen(argv[0]) > 1) { // First argument must be portno.
        args.port = argv[0];
        argc--;
        argv++;
    } else {
        throw_usage_error();
    }
    // Iterate over args while there are at least two arguments remaining, where
    // the first begins with "--" and the second is not empty.
    while (argc >= 2 && strncmp(argv[0], "--", 2) == 0 && strlen(argv[1]) > 0) {
        // At least two args, next begins with -- and following is not empty.
        process_arg(&args, &argc, &argv);
    }
    if (argc) { // Leftover arguments.
        throw_usage_error();
    }
    return args;
}

// process_arg()
//
// Assesses whether an argument from the command line is a valid argument.
// Exits appropriately if not.
// This function single-handedly made me despise pointers. I only really wrote
// this function to reduce the size of process_command_line, and the amount of
// work I had to do to get the pointers to work was a nightmare, so please if
// you're a tutor and you read this, go easy on me if the pointers are sloppy :)
void process_arg(CommandLineArgs* args, int* argc, char*** argv)
{
    if (!strcmp((*argv)[0], inArg) && args->io.inPath == NULL) {
        // Infile is present & unset.
        args->io.inPath = (*argv)[1];
        *argc -= 2;
        *argv += 2;
    } else if (!strcmp((*argv)[0], outputArg) && args->io.outPath == NULL) {
        // Outfile is present & unset.
        args->io.outPath = (*argv)[1];
        *argc -= 2;
        *argv += 2;
    } else if (args->op == UNSET && !strcmp((*argv)[0], rotateArg)
            && str_is_num((*argv)[1])
            && strtol((*argv)[1], NULL, BASE) < ROT_MAX
            && strtol((*argv)[1], NULL, BASE) > ROT_MIN) {
        // Valid rotate argument & no other image manip args set yet.
        args->op = ROTATE;
        args->rotate = strtol((*argv)[1], NULL, BASE);
        *argc -= 2;
        *argv += 2;
    } else if (args->op == UNSET && strcmp((*argv)[0], scaleArg) == 0
            && *argc >= (2 + 1) && str_is_num((*argv)[1])
            && str_is_num((*argv)[2])
            && strtol((*argv)[1], NULL, BASE) < SCALE_MAX
            && strtol((*argv)[2], NULL, BASE) < SCALE_MAX) {
        // Valid scale argument & no other image manip args set yet.
        args->op = SCALE;
        args->scale.x = atoi((*argv)[1]);
        args->scale.y = atoi((*argv)[2]);
        *argc -= (2 + 1);
        *argv += (2 + 1);
    } else if (strcmp((*argv)[0], flipArg) == 0 && args->op == UNSET
            && strlen((*argv)[1]) == 1
            && ((*argv)[1][0] == 'h' || (*argv)[1][0] == 'v')) {
        // Valid flip argument & no other image manip args set yet.
        args->op = FLIP;
        args->flip = (*argv)[1][0];
        *argc -= 2;
        *argv += 2;
    } else {
        // Invalid argument format.
        throw_usage_error();
    }
}

// str_is_num()
//
// Checks if all characters in given string are digits. Also accounts for an
// optional '-' or '+' indicator.
bool str_is_num(const char* str)
{
    bool strIsNum = true;

    for (size_t i = 0; i < strlen(str); i++) {
        if (!isdigit(str[i])) {
            // If any character in the string is not a digit, then string is not
            // an integer (except for a precursive '-' or '+' indicator).
            if (i == 0 && (str[i] == '-' || str[i] == '+')) {
                continue;
            }
            strIsNum = false;
        }
    }

    return strIsNum;
}

// check_files()
//
// Checks if given string representations of input/output files can be opened
// for reading/writing.
Io check_files(Io io)
{
    // Attempt to open input file for reading.
    if (io.inPath != NULL) {
        io.in = fopen(io.inPath, "r");
        if (io.in == NULL) {
            throw_read_error(io.inPath);
        }
    } else {
        io.in = stdin;
    }

    // Attempt to open output file for writing.
    if (io.outPath != NULL) {
        io.out = fopen(io.outPath, "w");
        if (io.out == NULL) {
            throw_write_error(io.outPath);
        }
    } else {
        io.out = stdout;
    }

    return io;
}

// check_connect()
//
// Checks if current process can establish a connection to a given port. Returns
// IP address info upon success.
// REF: This function uses code from "net2.c" in CSSE2310 public resources,
// REF: written and provided by Peter Sutton.
Connection check_connect(const char* port)
{
    struct addrinfo* ai = NULL;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    int err;
    if ((err = getaddrinfo(defaultHost, port, &hints, &ai))) {
        // Invalid port/ could simply not work out the address.
        freeaddrinfo(ai);
        throw_port_error(port); // Exits within call to this function.
    }

    // Create socket.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, ai->ai_addr, sizeof(struct sockaddr))) {
        throw_port_error(port); // Exits within call to this function.
    }

    // Create a new 'connection' after finding out program can connect to
    // server.
    Connection cnxn = {NULL, NULL};

    // Create seperate streams for reading & writing.
    int fd2 = dup(fd);
    cnxn.to = fdopen(fd, "w");
    cnxn.from = fdopen(fd2, "r");

    return cnxn;
}

// read_image()
//
// Reads data in from a given image and returns a buffer containing said data.
// REF: Code from Week 3.2 EdLessons "Reading from the filesystem" is used in
// REF: this function.
Data read_image(FILE* in)
{
    Data data = {NULL, 0};

    int bufferSize = INITIAL_BUFFER_SIZE;
    data.content = malloc(sizeof(unsigned char) * bufferSize);
    int next;

    if (feof(in)) {
        // Error in image data.
        free(data.content);
        throw_image_error();
    }

    // Continuously read data and resize buffer as necessary until EOF is
    // detected.
    while (1) {
        next = fgetc(in);
        if (next == EOF && data.nBytes == 0) {
            // Error in image data (no data to be read in).
            free(data.content);
            throw_image_error();
        }
        if (data.nBytes == bufferSize - 1) {
            bufferSize *= 2;
            data.content
                    = realloc(data.content, sizeof(unsigned char) * bufferSize);
        }
        if (next == EOF) {
            break;
        }
        data.content[(data.nBytes)++] = next;
    }

    return data;
}

// send_request()
//
// Constructs and sends an HTTP request containing given information to a given
// FILE*.
void send_request(FILE* to, CommandLineArgs args, Data data)
{
    // Send request type in request line.
    fprintf(to, "POST ");

    // Send address.
    switch (args.op) {
    case ROTATE:
        fprintf(to, "/%s,%i ", rotateOp, args.rotate);
        break;
    case SCALE:
        fprintf(to, "/%s,%i,%i ", scaleOp, args.scale.x, args.scale.y);
        break;
    case FLIP:
        fprintf(to, "/%s,%c ", flipOp, args.flip);
        break;
    case UNSET:
        fprintf(to, "/%s,%i ", defaultOp, defaultArg);
        break;
    }

    // Send protocol version.
    fprintf(to, "HTTP/1.1\r\n");

    // Send Content-Length Header.
    fprintf(to, "Content-Length: %i\r\n", data.nBytes);

    // Send blank line.
    fprintf(to, "\r\n");

    // Send Body - binary data.
    fwrite(data.content, sizeof(unsigned char), data.nBytes, to);
    fprintf(to, "\r\n");
    fflush(to);

    fclose(to);
}

// get_response()
//
// Populates a Response struct with the values returned from the provided
// get_HTTP_response function.
Response get_response(FILE* from)
{
    Response res = {0, NULL, NULL, 0, NULL};

    get_HTTP_response(from, &(res.status), &(res.statusExplanation),
            &(res.headers), &(res.body), &(res.len));
    // Error handle

    return res;
}

// handle_response()
//
// Handles a response from server populated by get_HTTP_response function.
// Outputs body response to appropriate out stream and exits with appropriate
// exit code.
void handle_response(Response res, Io io)
{
    if (res.status == OK) {
        // Success.
        fwrite(res.body, sizeof(unsigned char), res.len, io.out);
        exit(SUCCESS); ////////////////////////////////////////////////////////////////////////////////////////HANDLE
                       /// SIGPIPE
    } else {
        // Error response.
        fwrite(res.body, sizeof(unsigned char), res.len, stderr);
        throw_response_error(); //////////////////////////////////////////////////////////////////EXIT
                                /// WITH 11 (SERVER COMM ERROR)
    }
}

////////////////////////////////////////////////////////////////////////////////

// throw_usage_eror()
//
// Prints a usage error message and exits with the appropriate code.
void throw_usage_error(void)
{
    fprintf(stderr, errorUsage);
    exit(USAGE_ERROR);
}

// throw_read_error()
//
// Prints an error message about being unable to open a file for reading and
// exits with appropriate code.
void throw_read_error(const char* filename)
{
    fprintf(stderr, errorRead, filename);
    exit(READ_ERROR);
}

// throw_write_error()
//
// Prints an error message about being unable to open a file for writing and
// exits with appropriate code.
void throw_write_error(const char* filename)
{
    fprintf(stderr, errorWrite, filename);
    exit(WRITE_ERROR);
}

// throw_port_error()
//
// Prints an error message about not being able to connect to a port and exits
// with appropriate exit code.
void throw_port_error(const char* portname)
{
    fprintf(stderr, errorPort, portname);
    exit(PORT_ERROR);
}

// throw_image_error()
//
// Prints an error message about a lack of data in the image and exits with
// appropriate exit code.
void throw_image_error(void)
{
    fprintf(stderr, errorImage);
    exit(IMAGE_ERROR);
}

// throw_output_error()
//
// Prints an error message about being unable to write output and exits with
// appropriate exit code.
void throw_output_error(void)
{
    fprintf(stderr, errorOutput);
    exit(OUTPUT_ERROR);
}

// throw_response_error()
//
// Exits with appropriate exit code for errors relating to an error response
// from the server.
void throw_response_error(void)
{
    exit(RESPONSE_ERROR);
}

// throw_comm_error()
//
// Prints an error message about being unable to communicate with the server
// and exits with appropriate exit code.
void throw_comm_error(void)
{
    fprintf(stderr, errorComm);
    exit(COMM_ERROR);
}
