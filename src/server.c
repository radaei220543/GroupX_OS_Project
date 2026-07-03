#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define PORT 9090
#define HEADER_SIZE 8   // every message starts with an 8-byte label

// recv() can receive fewer bytes than requested in one call.
// Keep calling it until everything has actually arrived.
void recv_all(int socket_fd, void *data, size_t how_many_bytes) {
    size_t received_so_far = 0;
    while (received_so_far < how_many_bytes) {
        ssize_t got_now = recv(socket_fd, (char *)data + received_so_far,
                                how_many_bytes - received_so_far, 0);
        if (got_now <= 0) { perror("recv failed"); exit(1); } // safety check
        received_so_far += got_now;
    }
}

// Handles one connected client: for now just reads their request
// header and prints what piece they asked for
void handle_one_client(int client_socket, FILE *file, long file_size) {

    // Read the 8-byte label the client sends: total pieces + piece number
    uint8_t request_label[HEADER_SIZE];
    recv_all(client_socket, request_label, HEADER_SIZE);

    uint32_t total_pieces, this_piece_number;
    memcpy(&total_pieces,      request_label,     4);
    memcpy(&this_piece_number, request_label + 4, 4);
    total_pieces      = ntohl(total_pieces);       // fix number format
    this_piece_number = ntohl(this_piece_number);

    printf("Client wants piece %u of %u\n", this_piece_number, total_pieces);

    close(client_socket);
}

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

    // Wait for a connection, clone yourself, the clone
    // handles that connection completely then exits, the original
    // waits right here until the clone is done, then repeat
    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) continue; // safety check, try again

        pid_t pid = fork();

        if (pid == 0) {
            // everything here runs only in the clone
            close(server_socket);   // the clone doesn't need this
            handle_one_client(client_socket, file, file_size);
            exit(0);                // clone's job is done
        }

        // everything below runs only in the original process
        close(client_socket);       // the original doesn't need this
        waitpid(pid, NULL, 0);      // wait right here until the clone finishes
    }

    return 0;
}
