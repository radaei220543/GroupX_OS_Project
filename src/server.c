#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9090
#define HEADER_SIZE 8   // every message starts with an 8-byte label

int main(int argc, char *argv[]) {

    // If file name is more than two words, produce error
    if (argc != 2) {
        fprintf(stderr, "Usage: ./server <input_file>\n"); //example ./server original.dat
        return 1;
    }
    char *filename = argv[1];

    // To find file size, seek to the end of the file, then
    // assign to a variable, then reset position
    FILE *file = fopen(filename, "rb");
    if (!file) { perror("Could not open input file"); return 1; }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("File: %s (%ld bytes)\n", filename, file_size);

    // Get a socket for internet, reliable connections
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { perror("socket failed"); return 1; } // safety check

    // Fill out the address form: internet address, listen on any
    // of the network addresses, use port 9090
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));       // sets the form clean first
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);       // htons = convert to network byte order

    // Attach the address to the socket, then start listening for
    // incoming connections (up to 50 waiting in line at once)
    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); return 1; // safety check
    }
    if (listen(server_socket, 50) < 0) {
        perror("listen failed"); return 1; // safety check
    }

    printf("Listening on port %d ... (Ctrl+C to stop)\n", PORT);

    return 0;
}
