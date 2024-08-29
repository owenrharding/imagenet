// OWEN HARDING 48007618 CSSE2310 A4

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <csse2310a4.h>
#include <FreeImage.h>
#include <csse2310_freeimage.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

////////////////////////////////////////////////////////////////////////////////

// Enum Definitions.

// Exit Status Values.
typedef enum { USAGE_ERROR = 16, PORT_ERROR = 17 } ExitCodes;

// HTTP Response Status Codes.
typedef enum {
    INVALID_METHOD = 405,
    INVALID_ADDR = 404,
    INVALID_OP = 400,
    IMG_TOO_LRG = 413,
    INVALID_IMG = 422,
    OP_ERROR = 501,
    OK = 200
} ResponseCodes;

// Operation Type Flags.
typedef enum { ROTATE, FLIP, SCALE, NONE } OperationType;

// Types of changes to a stat variable - INCrement or DECrement.
typedef enum { INC, DEC } StatChange;

////////////////////////////////////////////////////////////////////////////////

// Structure Definitions.

// Structure to hold the program arguments - determined from command line.
typedef struct {
    char* port;
    long int maxClients;
} CommandLineArgs;

// Structure to hold the components of a socket based server.
typedef struct {
    int listenfd;
    short unsigned int port;
} Server;

// Structure to hold the to and from streams with a client.
typedef struct {
    FILE* to;
    FILE* from;
    sem_t* lim;
    sem_t* lock;
    int* activeClients;
    int* clientCount;
    int* okResponses;
    int* errorResponses;
    int* imageOps;
} Connection;

// Structure to hold the content and number of bytes in content read in from a
// file stream.
typedef struct {
    unsigned char* content;
    int nBytes;
} Data;

// Structure to hold the arguments of a HTTP request, populated by provided
// get_HTTP_request function.
typedef struct {
    char* method;
    char* address;
    unsigned char* body;
    unsigned long bodySize;
    HttpHeader** headers;
} Request;

// Structure to hold the arguments of a HTTP response to be sent back to a
// client.
typedef struct {
    int code;
    char* status;
    unsigned char* body;
    size_t bodySize;
    HttpHeader** headers;
} Response;

// Structure to hold the x and y values of a scale operation.
typedef struct {
    int x;
    int y;
} Scale;

// Structure to hold an operation and its argument.
typedef struct {
    OperationType opType;
    int rotate;
    char flip;
    Scale scale;
} Operation;

// Structure to hold an array of Operations.
typedef struct {
    Operation* ops;
    int numOps;
} OpArray;

// Structure to hold information and data from the execution of an OpArray.
typedef struct {
    int successCount;
    OperationType opFail;
    FIBITMAP* bitmap;
} OpExecInfo;

// Structure to hold the data that gets passed into a client thread.
typedef struct {
    Connection* cnxn;
    sem_t* lim;
} ClientData;

// Structure to hold the program statistics.
typedef struct {
    int activeClients;
    int clientCount;
    int okResponses;
    int errorResponses;
    int imageOps;
} Statistics;

// Structure to hold the data passed to the signal handling thread.
typedef struct {
    sigset_t set;
    int* activeClients;
    int* clientCount;
    int* okResponses;
    int* errorResponses;
    int* imageOps;
} SigData;

////////////////////////////////////////////////////////////////////////////////

#define BASE_10 10
#define MAX_SCALE 10000
#define MIN_SCALE 1
#define MAX_ROTATE 360
#define MIN_ROTATE (-360)

#define INITIAL_BUFFER_SIZE 256
#define MAX_IMAGE_SIZE 8388608
#define MAX_CLIENTS_LIMIT 510

const char* const portDelim = "--listenOn";
const char* const maxDelim = "--max";

const char* const rotateWord = "rotate";
const char* const flipWord = "flip";
const char* const scaleWord = "scale";

// Error Messages.
const char* const errorUsage
        = "Usage: uqimageproc [--listenOn portnum] [--max num]\n";
const char* const errorPort
        = "uqimageproc: cannot listen on given port \"%s\"\n";

const char* const homePageAddress
        = "/local/courses/csse2310/resources/a4/home.html";

// HTTP Response Header Delimeters.
const char* const contentTypeHeader = "Content-Type";
const char* const contentLengthHeader = "Content-Length";

// HTTP Response Statuses.
const char* const invalidMethodStatus = "Method Not Allowed";
const char* const invalidAddressStatus = "Not Found";
const char* const homePageStatus = "OK";
const char* const invalidOpStatus = "Bad Request";
const char* const imageTooLargeStatus = "Payload Too Large";
const char* const invalidImageStatus = "Unprocessable Content";
const char* const opErrorStatus = "Not Implemented";
const char* const imageManipStatus = "OK";

// HTTP Response Messages.
const char* const invalidMethodMessage = "Invalid method on request list\n";
const char* const invalidAddressMessage = "Invalid address in GET request\n";
const char* const invalidOpMessage = "Invalid operation requested\n";
const char* const imageTooLargeMessage
        = "Image received is too large: %li bytes\n";
const char* const invalidImageMessage = "Invalid image received\n";
const char* const opErrorMessage = "Operation failed: %s\n";

const char* const connectedClientsMessage = "Currently connected clients: %i\n";
const char* const servicedClientsMessage = "Serviced clients: %i\n";
const char* const successRequestsMessage = "HTTP requests successful: %i\n";
const char* const errorRequestsMessage = "HTTP error responses: %i\n";
const char* const imageOpsMessage = "Image operations: %i\n";

////////////////////////////////////////////////////////////////////////////////

// Function Prototypes.

// Command Line Processing.
CommandLineArgs process_command_line(int argc, char* argv[]);
Server check_listen(CommandLineArgs args);

// Runtime Behaviour.
void* client_thread(void* arg);
void process_connections(int fdServer, sem_t lim, sem_t lock, Statistics stats);

// HTTP Response Behaviour.
Response construct_response(
        Request req, Response res, sem_t* lock, int* imageOps);
HttpHeader** init_headers_array(
        HttpHeader** headers, char* contentType, int contentLength);
void send_response(Response res, FILE* sendto);

// Image Manipulation Functionality.
OpArray extract_ops_from_http_addr(char** addr);
OpArray add_op_to_array(OpArray opArr, Operation op);
OpExecInfo execute_ops(OpExecInfo info, OpArray opArr, FIBITMAP* dib);
void handle_flip(OpExecInfo* info, FIBITMAP* dib, char flip);

// Semaphore Functionality - Connection Limiting & Mutual Exclusion.
void init_lim(sem_t* lim, int maxClients);
void init_lock(sem_t* lock);
void take_lock(sem_t* lock);
void release_lock(sem_t* lock);
void modify_stat(sem_t* lock, int* stat, StatChange change, int amount);
void print_statistics(int activeClients, int clientCount, int okResponses,
        int errorResponses, int imageOps);

// Helper Functions.
Data read_data(FILE* in);
bool str_is_num(const char* str);

// HTTP Response Initialisers.
Response init_invalid_method_response(Response res);
Response init_invalid_address_response(Response res);
Response init_home_page_response(Response res);
Response init_invalid_op_response(Response res);
Response init_image_too_large_response(Response res, int nBytes);
Response init_invalid_image_response(Response res);
Response init_op_error_response(Response res, OperationType opType);
Response init_image_manip_response(
        Response res, unsigned char* buffer, const unsigned long* numBytes);

// Error Messages.
void throw_usage_error(void);
void throw_port_error(const char* port);

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    CommandLineArgs args = process_command_line(argc, argv);

    Server serv = check_listen(args);

    fprintf(stderr, "%u\n", serv.port);
    fflush(stderr);

    sem_t lim;
    init_lim(&lim, args.maxClients); // Connection limiting semaphore.
    sem_t lock;
    init_lock(&lock);

    Statistics stats = {0, 0, 0, 0, 0};

    process_connections(serv.listenfd, lim, lock, stats);
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
    CommandLineArgs args = {NULL, 0};

    // Skip over program name.
    argc--;
    argv++;

    // Iterate over args while there are at least two arguments remaining, where
    // the first begins with "--" and the second is not empty.
    while (argc >= 2 && strncmp(argv[0], "--", 2) == 0 && strlen(argv[1]) > 0) {
        // At least two args, next begins with -- and following is not empty.
        if (!strcmp(argv[0], portDelim) && !args.port) {
            // Port arg is present and unset.
            args.port = argv[1];
            argc -= 2;
            argv += 2;
        } else if (!strcmp(argv[0], maxDelim) && !args.maxClients
                && str_is_num(argv[1]) && strtol(argv[1], NULL, BASE_10) > 0
                && strtol(argv[1], NULL, BASE_10) <= MAX_SCALE) {
            // Max arg is present, unset, and a positive int less than 10000.
            args.maxClients = strtol(argv[1], NULL, BASE_10);
            argc -= 2;
            argv += 2;
        } else {
            throw_usage_error();
        }
    }
    if (argc) {
        // More args still remain which shouldn't be there.
        throw_usage_error();
    }
    if (args.port == NULL) {
        // If port is unset, set it to 0 so it turns into an ephemeral port.
        args.port = "0";
    }

    return args;
}

// check_listen()
//
// Implementation of Port Checking functionality. Checks socket can be created
// from port given on command line (or assigns ephemeral port), checks socket
// can be bound to, and checks socket can be listened on. If any of the
// aforementioned checks fail, exits with appropriate code. Returns struct
// Server containing relevant data.
// REF: Code in this function comes from 'net4.c' and 'server-multithreaded.c',
// REF: provided as a CSSE2310 resource, written by Peter Sutton.
Server check_listen(CommandLineArgs args)
{
    Server serv = {0, 0};

    struct addrinfo* ai = 0;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // Will allow us to bind the socket to all of
                                 // our interfaces (addresses) but only if the
                                 // first argument to getaddrinfo() is NULL)

    int err;
    if ((err = getaddrinfo(NULL, args.port, &hints, &ai))) {
        freeaddrinfo(ai);
        throw_port_error(args.port);
    }

    // create a socket and bind it to ain address/port.
    serv.listenfd = socket(AF_INET, SOCK_STREAM, 0); // 0=default prot (TCP)

    // Allow address (port number) to be reused immediately.
    int optVal = 1;
    if (setsockopt(
                serv.listenfd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int))
            < 0) {
        throw_port_error(args.port);
    }

    // Check socket can be bound to.
    if (bind(serv.listenfd, ai->ai_addr, sizeof(struct sockaddr))) {
        throw_port_error(args.port);
    }

    // Which port did we get?
    struct sockaddr_in ad;
    memset(&ad, 0, sizeof(struct sockaddr_in));
    socklen_t len = sizeof(struct sockaddr_in);
    int sock = getsockname(serv.listenfd, (struct sockaddr*)&ad, &len);
    if (sock) {
        throw_port_error(args.port);
    }
    serv.port = ntohs(ad.sin_port);

    if (listen(serv.listenfd, 0) < 0) { // Moss doesn't listen to 2nd arg.
        throw_port_error(args.port);
    }

    return serv;
}

// init_lim()
//
// Initialises client connection limiting semaphore. If given maxClients is set,
// then the program will limit the number of active clients to maxClients,
// otherwise an arbitrary amount can join.
void init_lim(sem_t* lim, int maxClients)
{
    if (maxClients > 0) {
        sem_init(lim, 0, maxClients);
    } else {
        sem_init(lim, 0, MAX_CLIENTS_LIMIT);
    }
}

// init_lock()
//
// Initialises the mutual exclusion lock semaphore.
void init_lock(sem_t* lock)
{
    sem_init(lock, 0, 1);
}

// take_lock()
//
// Allows a thread to 'take' current access to the mutual exclusion semaphore.
void take_lock(sem_t* lock)
{
    sem_wait(lock);
}

// release_lock()
//
// Allows a thread to 'release' possession of a given mutual exclusion
// semaphore, allowing other threads to 'take' it.
void release_lock(sem_t* lock)
{
    sem_post(lock);
}

// sig_thread()
//
// Function pointer thread dedicated for handling caught signals. Responds if
// SIGHUP is detected.
void* sig_thread(void* arg)
{
    SigData* data = (SigData*)arg;
    int sig;

    for (;;) {
        sigwait(&(data->set), &sig);
        if (sig == SIGHUP) {
            print_statistics(*(data->activeClients), *(data->clientCount),
                    *(data->okResponses), *(data->errorResponses),
                    *(data->imageOps));
        }
    }
}

// print_statistics()
//
// Prints and flushes the given current program statistics to stderr.
void print_statistics(int activeClients, int clientCount, int okResponses,
        int errorResponses, int imageOps)
{
    fprintf(stderr, connectedClientsMessage, activeClients);
    fprintf(stderr, servicedClientsMessage, clientCount);
    fprintf(stderr, successRequestsMessage, okResponses);
    fprintf(stderr, errorRequestsMessage, errorResponses);
    fprintf(stderr, imageOpsMessage, imageOps);
    fflush(stderr);
}

// process_connections()
//
// Process incoming client connections onto the open socket. Spawns new client
// threads for each client accepted.
void process_connections(int fdServer, sem_t lim, sem_t lock, Statistics stats)
{
    pthread_t sigHandler;
    SigData sigdata;
    int s;

    sigemptyset(&(sigdata.set));
    sigaddset(&(sigdata.set), SIGHUP);

    sigdata.activeClients = &(stats.activeClients);
    sigdata.clientCount = &(stats.clientCount);
    sigdata.okResponses = &(stats.okResponses);
    sigdata.errorResponses = &(stats.errorResponses);
    sigdata.imageOps = &(stats.imageOps);

    s = pthread_sigmask(SIG_BLOCK, &(sigdata.set), NULL);
    if (s != 0) {
        perror("Sigmask");
    }
    s = pthread_create(&sigHandler, NULL, &sig_thread, (void*)&(sigdata.set));

    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;

    // Repeatedly accept connections and process data
    while (1) {
        // Semaphore-based client connection limiting.
        sem_wait(&lim);

        fromAddrSize = sizeof(struct sockaddr_in);
        // Waiting for a new connection (fromAddr will be populated
        // with address of client).
        fd = accept(fdServer, (struct sockaddr*)&fromAddr, &fromAddrSize);

        // Create new two way connection with client.
        Connection cnxn = {NULL, NULL, &lim, &lock, &(stats.activeClients),
                &(stats.clientCount), &(stats.okResponses),
                &(stats.errorResponses), &(stats.imageOps)};
        int fd2 = dup(fd);
        cnxn.to = fdopen(fd, "w");
        cnxn.from = fdopen(fd2, "r");

        Connection* data = malloc(sizeof(Connection));
        *data = cnxn;

        pthread_t threadID;
        pthread_create(&threadID, NULL, client_thread, data);
        pthread_detach(threadID);
    }
}

// client_thread()
//
// Thread function that handles a single client. Gets HTTP request from client
// and sends response accordingly.
// REF: This function is based off the client thread function in
// REF: 'server-multithreaded.c', which was written and provided by Peter
// REF: Sutton.
void* client_thread(void* arg)
{
    Connection cnxn = *(Connection*)arg;
    free(arg);

    modify_stat(cnxn.lock, (cnxn.activeClients), INC, 1);

    // Initialise request to be received from client.
    Request req = {NULL, NULL, NULL, 0, NULL};

    while (get_HTTP_request(cnxn.from, &(req.method), &(req.address),
                   &(req.headers), &(req.body), &(req.bodySize))
            != 0) {
        // Initialise response to be sent back.
        Response res = {0, NULL, NULL, 0, NULL};

        // Construct appropriate HTTP response.
        res = construct_response(req, res, cnxn.lock, (cnxn.imageOps));

        if (res.code == OK) {
            modify_stat(cnxn.lock, (cnxn.okResponses), INC, 1);
        } else {
            modify_stat(cnxn.lock, (cnxn.errorResponses), INC, 1);
        }

        // Send response.
        send_response(res, cnxn.to);

        fflush(cnxn.to);
        fflush(stdout);
    }

    fclose(cnxn.to);
    fclose(cnxn.from);

    sem_post(cnxn.lim);

    modify_stat(cnxn.lock, (cnxn.activeClients), DEC, 1);
    modify_stat(cnxn.lock, (cnxn.clientCount), INC, 1);

    return NULL; // could have called pthread_exit(NULL);
}

void modify_stat(sem_t* lock, int* stat, StatChange change, int amount)
{
    take_lock(lock);
    if (change == INC) {
        (*stat) += amount;
    } else if (change == DEC) {
        (*stat) -= amount;
    }
    release_lock(lock);
}

// construct_response()
//
// Constructs the appropriate HTTP response based on the given request.
// Populates given Response struct with appropriate elements.
Response construct_response(
        Request req, Response res, sem_t* lock, int* imageOps)
{
    if (!strcmp(req.method, "GET")) {
        if (strcmp(req.address, "/") != 0) {
            // Invalid address for GET request.
            res = init_invalid_address_response(res);
        } else {
            // Valid home page response.
            res = init_home_page_response(res);
        }
    } else if (!strcmp(req.method, "POST")) {
        char* addr = strdup(req.address);
        char** splitAddr
                = split_by_char((addr + 1), '/', 0); // +1 skip first '/'.
        OpArray opArr = extract_ops_from_http_addr(splitAddr);
        if (!opArr.ops) {
            // Invalid operation in address, extract returned NULL.
            res = init_invalid_op_response(res);
        } else if (req.bodySize > MAX_IMAGE_SIZE) {
            // Body provided in HTTP request too large.
            res = init_image_too_large_response(res, req.bodySize);
        } else {
            // Load FreeImage bitmap.
            FIBITMAP* bitmap
                    = fi_load_image_from_buffer(req.body, req.bodySize);
            if (bitmap == NULL) { // Error loading FreeImage bitmap.
                res = init_invalid_image_response(res);
            } else { // Valid FreeImage load.
                OpExecInfo info = {0, NONE, NULL};
                info = execute_ops(info, opArr, bitmap);
                if (info.successCount) {
                    modify_stat(lock, imageOps, INC, info.successCount);
                }
                if (info.opFail != NONE) { // Operation error fail.
                    init_op_error_response(res, info.opFail);
                } else {
                    unsigned long* numBytes = malloc(sizeof(unsigned long*));
                    unsigned char* buffer = fi_save_png_image_to_buffer(
                            info.bitmap, numBytes);
                    res = init_image_manip_response(res, buffer, numBytes);
                    FreeImage_Unload(info.bitmap);
                }
            }
        }
    } else {
        // Invalid method.
        res = init_invalid_method_response(res);
    }
    return res;
}

// extract_ops_from_http_addr()
//
// Iterates through the given char** which has been split at every instance of
// the character '/', and checks whether it fits the required POST address
// format. If it does, creates a new Operation struct and adds it to a growing
// array of Operations. Returns NULL if there is an invalid operation.
OpArray extract_ops_from_http_addr(char** addr)
{
    OpArray opArr = {NULL, 0};

    for (int i = 0; addr[i] != NULL; i++) {
        char** opArg = split_by_char(addr[i], ',', 0);

        // Operation matches one of rotate, flip, or scale.
        if (!strcmp(opArg[0], "rotate") && opArg[2] == NULL
                && str_is_num(opArg[1])
                && strtol(opArg[1], NULL, BASE_10) > MIN_ROTATE
                && strtol(opArg[1], NULL, BASE_10) < MAX_ROTATE) {
            // Valid rotate operation.
            Operation rotateOp
                    = {ROTATE, strtol(opArg[1], NULL, BASE_10), '\0', {0, 0}};
            opArr = add_op_to_array(opArr, rotateOp);
        } else if (!strcmp(opArg[0], "flip") && opArg[2] == NULL
                && strlen(opArg[1]) == 1
                && (opArg[1][0] == 'h' || opArg[1][0] == 'v')) {
            // Valid flip operation.
            Operation flipOp = {FLIP, 0, opArg[1][0], {0, 0}};
            opArr = add_op_to_array(opArr, flipOp);
        } else if (!strcmp(opArg[0], "scale") && opArg[2 + 1] == NULL
                && str_is_num(opArg[1]) && str_is_num(opArg[2])
                && strtol(opArg[1], NULL, BASE_10) >= MIN_SCALE
                && strtol(opArg[1], NULL, BASE_10) <= MAX_SCALE
                && strtol(opArg[2], NULL, BASE_10) >= MIN_SCALE
                && strtol(opArg[2], NULL, BASE_10) <= MAX_SCALE) {
            // Valid scale operation.
            Operation scaleOp = {SCALE, 0, '\0',
                    {strtol(opArg[1], NULL, BASE_10),
                            strtol(opArg[2], NULL, BASE_10)}};
            opArr = add_op_to_array(opArr, scaleOp);
        } else {
            if (opArr.ops != NULL) {
                opArr.ops = NULL;
            }
            break;
        }
    }

    return opArr;
}

// add_op_to_array()
//
// Adds a given Operation struct to a given OpArray struct. Handles dynamic
// memory (re)allocation and appropriate resizing of the OpArray.
OpArray add_op_to_array(OpArray opArr, Operation op)
{
    // Grow the array of operations to hold one more.
    opArr.numOps++;
    opArr.ops = realloc(opArr.ops, opArr.numOps * sizeof(Operation));
    opArr.ops[opArr.numOps - 1] = op;
    return opArr;
}

// execute_ops()
//
// Handles rotate, flip, or scale operations from given OpArray onto given
// preloaded bitmap.
OpExecInfo execute_ops(OpExecInfo info, OpArray opArr, FIBITMAP* dib)
{
    // Iterate through operations and execute.
    for (int i = 0; i < opArr.numOps; i++) {
        if (opArr.ops[i].opType == ROTATE) {
            // Rotate image.
            info.bitmap
                    = FreeImage_Rotate(dib, (double)opArr.ops[i].rotate, NULL);
            if (info.bitmap != NULL) {
                // Successful rotate.
                (info.successCount)++;
            } else {
                info.opFail = ROTATE;
            }
        } else if (opArr.ops[i].opType == FLIP) {
            // Flip image.
            handle_flip(&info, dib, opArr.ops[i].flip);
        } else if (opArr.ops[i].opType == SCALE) {
            // Scale image.
            info.bitmap = FreeImage_Rescale(dib, opArr.ops[i].scale.x,
                    opArr.ops[i].scale.y, FILTER_BILINEAR);
            if (info.bitmap != NULL) {
                // Successful rescale.
                (info.successCount)++;
            } else {
                info.opFail = SCALE;
            }
        }
        FreeImage_Unload(dib);
        if (info.opFail != NONE) {
            // Something has failed, do not attempt to execute another op.
            FreeImage_Unload(info.bitmap);
            break;
        }
        dib = info.bitmap;
    }
    return info;
}

// handle_flip()
//
// Handles an image flip given the direction. Updates the FreeImage BitMap
// stored in the given OpExecInfo, done situationally based on whether it failed
// or not.
void handle_flip(OpExecInfo* info, FIBITMAP* dib, char flip)
{
    int flipSuccess = 0;
    if (flip == 'h') {
        flipSuccess = FreeImage_FlipHorizontal(dib);
    } else if (flip == 'v') {
        flipSuccess = FreeImage_FlipVertical(dib);
    }

    if (flipSuccess) {
        // Successful flip
        (info->successCount)++;
        info->bitmap
                = FreeImage_Clone(dib); // Clone so original can be unloaded
    } else {
        info->opFail = FLIP;
    }
}

////////////////////////////////////////////////////////////////////////////////

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

// read_data()
//
// Reads data in from a given file stream and returns a buffer containing the
// data.
// REF: Code from Week 3.2 EdLessons "Reading from the filesystem" is used in
// REF: this function.
Data read_data(FILE* in)
{
    Data data = {NULL, 0};

    int bufferSize = INITIAL_BUFFER_SIZE;
    data.content = malloc(sizeof(unsigned char) * bufferSize);
    int next;

    if (feof(in)) {
        // Error in image data.
        free(data.content);
    }

    // Continuously read data and resize buffer as necessary until EOF is
    // detected.
    while (1) {
        next = fgetc(in);
        if (next == EOF && data.nBytes == 0) {
            // Error in image data (no data to be read in).
            free(data.content);
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

////////////////////////////////////////////////////////////////////////////////

// init_headers_array()
//
// Initialises array of HTTP headers, given the Content Type and Content Length.
// Content Type should be NULL if not present, and Content Length should be 0
// if not present.
HttpHeader** init_headers_array(
        HttpHeader** headers, char* contentType, int contentLength)
{
    // Initialise headers array.
    int numHeaders = 2;
    // Add 1 to malloc size for NULL terminating pointer.
    headers = malloc((numHeaders + 1) * sizeof(HttpHeader*));
    headers[0] = malloc(sizeof(HttpHeader));
    headers[1] = malloc(sizeof(HttpHeader));
    headers[2] = NULL;

    // Initialise Content-Type header.
    headers[0]->name = strdup(contentTypeHeader);
    headers[0]->value = strdup(contentType);

    // Turn contentLength into string for Content-Length Header.
    char contentLengthS[INITIAL_BUFFER_SIZE];
    snprintf(contentLengthS, sizeof(contentLengthS), "%i", contentLength);

    // Initialise Content-Length Header
    headers[1]->name = strdup(contentLengthHeader);
    headers[1]->value = strdup(contentLengthS);

    return headers;
}

// send_response()
//
// Sends a given HTTP response to a given file stream. Frees malloced memory in
// the given Response struct.
void send_response(Response res, FILE* sendto)
{
    unsigned long len;

    unsigned char* response = construct_HTTP_response((int)(res.code),
            (const char*)(res.status), res.headers,
            (const unsigned char*)(res.body), (unsigned long)(res.bodySize),
            &len);

    free(res.status);
    free(res.body);
    free_array_of_headers(res.headers);

    fwrite(response, sizeof(unsigned char), len, sendto);
}

// init_invalid_method_response()
//
// Initialises a struct Response with all the appropriate components to
// construct an invalid method HTTP response.
Response init_invalid_method_response(Response res)
{
    res.code = INVALID_METHOD;
    res.status = strdup(invalidMethodStatus);
    res.body = (unsigned char*)strdup(invalidMethodMessage);
    res.bodySize = strlen((char*)(res.body));
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_invalid_address_response()
//
// Initialises a struct Response with all the appropriate components to
// construct an invalid address HTTP response.
Response init_invalid_address_response(Response res)
{
    res.code = INVALID_ADDR;
    res.status = strdup(invalidAddressStatus);
    res.body = (unsigned char*)strdup(invalidAddressMessage);
    res.bodySize = strlen((char*)(res.body));
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_home_page_response()
//
// Populates a given struct Response with all the appropriate components to
// construct a home page HTTP response.
Response init_home_page_response(Response res)
{
    res.code = OK;
    res.status = strdup(homePageStatus);
    FILE* homePage = fopen(homePageAddress, "r");
    Data data = read_data(homePage);
    res.body = (unsigned char*)data.content;
    res.bodySize = data.nBytes;
    res.headers = init_headers_array(res.headers, "text/html", res.bodySize);

    return res;
}

// init_invalid_op_response()
//
// Populates a given struct Response with all the appropriate components to
// construct an invalid operation HTTP response.
Response init_invalid_op_response(Response res)
{
    res.code = INVALID_OP;
    res.status = strdup(invalidOpStatus);
    res.body = (unsigned char*)strdup(invalidOpMessage);
    res.bodySize = strlen((char*)(res.body));
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_image_too_large_response()
//
// Populates a given struct Response with all the appropriate components to
// construct an image too large HTTP response.
Response init_image_too_large_response(Response res, int nBytes)
{
    res.code = IMG_TOO_LRG;
    res.status = strdup(imageTooLargeStatus);
    char bodyMessage[INITIAL_BUFFER_SIZE]; // Initialise char* filler to be
                                           // sprintf-ed into.
    res.bodySize = sprintf(bodyMessage, imageTooLargeMessage, nBytes);
    res.body = (unsigned char*)strdup(bodyMessage);
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_invalid_image_response()
//
// Populates a given struct Response with all the appropriate components to
// construct a invalid image HTTP response.
Response init_invalid_image_response(Response res)
{
    res.code = INVALID_IMG;
    res.status = strdup(invalidImageStatus);
    res.body = (unsigned char*)strdup(invalidImageMessage);
    res.bodySize = strlen((char*)(res.body));
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_op_error_response()
//
// Populates a given struct Response with all the appropriate components to
// construct an operation error HTTP response.
Response init_op_error_response(Response res, OperationType opType)
{
    char* op;
    if (opType == ROTATE) {
        op = strdup(rotateWord);
    } else if (opType == FLIP) {
        op = strdup(flipWord);
    } else if (opType == SCALE) {
        op = strdup(scaleWord);
    }

    res.code = OP_ERROR;
    res.status = strdup(opErrorStatus);
    char bodyMessage[INITIAL_BUFFER_SIZE]; // Initialise char* filler to be
                                           // sprintf-ed into.
    res.bodySize = sprintf(bodyMessage, opErrorMessage, op);
    res.body = (unsigned char*)strdup(bodyMessage);
    res.headers = init_headers_array(res.headers, "text/plain", res.bodySize);

    return res;
}

// init_image_manip_response()
//
// Populates a given struct Reponse with all the appropriate componets to
// construct an image manipulation HTTP response.
Response init_image_manip_response(
        Response res, unsigned char* buffer, const unsigned long* numBytes)
{
    res.code = OK;
    res.status = strdup(imageManipStatus);
    res.body = buffer;
    res.bodySize = *numBytes;
    res.headers = init_headers_array(res.headers, "image/png", res.bodySize);

    return res;
}

////////////////////////////////////////////////////////////////////////////////

// throw_usage_error()
//
// Prints a usage error message and exits with the appropriate code.
void throw_usage_error(void)
{
    fprintf(stderr, errorUsage);
    exit(USAGE_ERROR);
}

// throw_port_error()
//
// Prints an error message about not being able to listen on a given port and
// exits with appropriate code.
void throw_port_error(const char* port)
{
    fprintf(stderr, errorPort, port);
    exit(PORT_ERROR);
}
