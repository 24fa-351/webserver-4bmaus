#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "webserver.h"

int main(int argc, char *argv[]) {
    int port = 80; // Default port

    // Check for "-p" option to specify a different port
    if (argc > 2 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port number.\n");
            return 1;
        }
    }

    printf("Starting server on port %d\n", port);
    start_server(port);
    return 0;
}
