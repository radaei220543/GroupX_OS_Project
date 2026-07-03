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

// send() can send fewer bytes than requested in one call.
// Keep calling it until everything has actually been sent.
void send_all(int socket_fd, void *data, size_t how_many_bytes) {
    size_t sent_so_far = 0;
    while (sent_so_far < how_many_bytes) {
        ssize_t sent_now = send(socket_fd, (char *)data + sent_so_far,
                                 how_many_bytes - sent_so_far, 0);
        if (sent_now <= 0) { perror("send failed"); exit(1); } // if send() fails, stop
        sent_so_far += sent_now;
    }
}

// Handles one connected client: reads what piece they want, finds
// it in the file, and sends it back
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

    // Work out which numbers in the file belong to this piece
    long total_numbers     = file_size / 4;   // each number is 4 bytes
    long numbers_per_piece = (total_numbers + total_pieces - 1) / total_pieces;
    long start_number      = (this_piece_number - 1) * numbers_per_piece;

    long numbers_in_this_piece = numbers_per_piece;
    if (start_number + numbers_in_this_piece > total_numbers) {
        numbers_in_this_piece = total_numbers - start_number; // last piece: leftovers
    }
    long bytes_in_this_piece = numbers_in_this_piece * 4;

    // Jump to where this piece starts in the file, then read it
    fseek(file, start_number * 4, SEEK_SET);

    uint8_t *piece_data = malloc(bytes_in_this_piece);
    fread(piece_data, 1, bytes_in_this_piece, file);

    // Build the response label, then send it followed by the data
    uint8_t response_label[HEADER_SIZE];
    uint32_t piece_number_net = htonl(this_piece_number);
    uint32_t piece_size_net   = htonl((uint32_t)bytes_in_this_piece);
    memcpy(response_label,     &piece_number_net, 4);
    memcpy(response_label + 4, &piece_size_net,   4);

    send_all(client_socket, response_label, HEADER_SIZE);
    send_all(client_socket, piece_data, bytes_in_this_piece);

    printf("Sent piece %u (%ld bytes)\n", this_piece_number, bytes_in_this_piece);

    free(piece_data);
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

    // Get a socket for TCP
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) { perror("socket failed"); return 1; } // if socket() fails, returns value of -1

    // Fill out the address data structure: internet address, 
    // listen on any of the network addresses, use port 9090
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));       // sets the bytes of the structure to 0
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);       // htons = convert to network byte order

    // Attach the address to the socket, then start listening for
    // incoming connections (up to 50 waiting in line at once)
    struct sockaddr *generic_address = (struct sockaddr *)&address;
    int result = bind(server_socket, generic_address, sizeof(address));
    if (result < 0) {
        perror("bind failed");
        return 1;
    }

    int listen_result = listen(server_socket, 50);
    if (listen_result < 0) {
        perror("listen failed");
        return 1;
    }

    printf("Listening on port %d ... (Ctrl+C to stop)\n", PORT);

    // Wait for a connection, clone yourself, the clone
    // handles that connection completely then exits, the original
    // waits right here until the clone is done, then repeat
    while (1) {
        int client_socket = accept(server_socket, NULL, NULL); //pulls one connection from listen() queue
        if (client_socket < 0) continue; // loops back if failed instead of crashing

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
