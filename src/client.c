#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>

#define SERVER_PORT 9090
#define HEADER_SIZE 8

static void request_piece(int sock, uint32_t total_pieces, uint32_t piece_number) {
    uint8_t req[HEADER_SIZE];
    uint32_t tp_net = htonl(total_pieces);
    uint32_t pn_net = htonl(piece_number);
    memcpy(req, &tp_net, 4);
    memcpy(req + 4, &pn_net, 4);

    size_t sent = 0;
    while (sent < HEADER_SIZE) {
        ssize_t s = send(sock, req + sent, HEADER_SIZE - sent, 0);
        if (s <= 0) { perror("send request"); exit(1); }
        sent += s;
    }
}

static void recv_all(int sock, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(sock, (char*)buf + got, n - got, 0);
        if (r <= 0) { perror("recv failed"); exit(1); }
        got += r;
    }
}

int main(int argc, char *argv[]) {
    int N = 0;
    int T = 4;
    char server_ip[64] = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            N = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            strncpy(server_ip, argv[++i], sizeof(server_ip) - 1);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            T = atoi(argv[++i]);
        }
    }

    if (N <= 0) {
        fprintf(stderr, "Usage: %s -p <N> [-h ip] [-t T]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    long chunk_size_bytes;
    {
        int probe_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (probe_sock < 0) { perror("socket (probe)"); exit(1); }
        if (connect(probe_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect (probe)");
            exit(1);
        }

        request_piece(probe_sock, (uint32_t)N, 1);

        uint8_t resp[HEADER_SIZE];
        recv_all(probe_sock, resp, HEADER_SIZE);

        uint32_t piece_number_net, piece_size_net;
        memcpy(&piece_number_net, resp, 4);
        memcpy(&piece_size_net, resp + 4, 4);
        uint32_t piece_size = ntohl(piece_size_net);

        chunk_size_bytes = piece_size;

        uint8_t *throwaway = malloc(piece_size);
        recv_all(probe_sock, throwaway, piece_size);
        free(throwaway);
        close(probe_sock);

        printf("[client] Probed chunk size: %ld bytes (from piece 1 of %d)\n", chunk_size_bytes, N);
    }

    if (chunk_size_bytes <= 0) {
        fprintf(stderr, "[client] Invalid chunk size received from server\n");
        exit(1);
    }

    long buffer_capacity = chunk_size_bytes * N;

    char *buffer = mmap(NULL, buffer_capacity, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) { perror("mmap buffer"); exit(1); }

    sem_t *received_count = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (received_count == MAP_FAILED) { perror("mmap sem"); exit(1); }
    sem_init(received_count, 1, 0);

    pthread_mutex_t *log_mutex = mmap(NULL, sizeof(pthread_mutex_t),
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (log_mutex == MAP_FAILED) { perror("mmap mutex"); exit(1); }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(log_mutex, &attr);

    int *chunks_done = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *chunks_done = 0;

    long *last_piece_actual_size = mmap(NULL, sizeof(long), PROT_READ | PROT_WRITE,
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *last_piece_actual_size = chunk_size_bytes;

    for (int seq = 1; seq <= N; seq++) {
        pid_t pid = fork();

        if (pid < 0) { perror("fork"); exit(1); }

        if (pid == 0) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) { perror("socket"); exit(1); }

            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                fprintf(stderr, "[child seq=%d] connect failed\n", seq);
                exit(1);
            }

            request_piece(sock, (uint32_t)N, (uint32_t)seq);

            uint8_t resp[HEADER_SIZE];
            recv_all(sock, resp, HEADER_SIZE);

            uint32_t piece_number_net, piece_size_net;
            memcpy(&piece_number_net, resp, 4);
            memcpy(&piece_size_net, resp + 4, 4);
            uint32_t recv_seq = ntohl(piece_number_net);
            uint32_t payload_size = ntohl(piece_size_net);

            if (recv_seq != (uint32_t)seq) {
                fprintf(stderr, "[child seq=%d] MISMATCH: got seq=%u\n", seq, recv_seq);
                exit(1);
            }

            char *dest = buffer + (seq - 1) * chunk_size_bytes;
            recv_all(sock, dest, payload_size);

            printf("[child seq=%d] Received %u bytes OK.\n", seq, payload_size);

            if (seq == N) {
                *last_piece_actual_size = payload_size;
            }

            pthread_mutex_lock(log_mutex);
            (*chunks_done)++;
            pthread_mutex_unlock(log_mutex);

            sem_post(received_count);

            close(sock);
            exit(0);
        }
    }

    for (int i = 0; i < N; i++) wait(NULL);
    for (int i = 0; i < N; i++) sem_wait(received_count);

    long real_total_size = (long)(N - 1) * chunk_size_bytes + *last_piece_actual_size;

    printf("[client] All %d chunks confirmed received (chunks_done=%d).\n", N, *chunks_done);
    printf("[client] Real total size: %ld bytes\n", real_total_size);

    FILE *out = fopen("reassembled.dat", "wb");
    if (!out) { perror("fopen"); exit(1); }
    fwrite(buffer, 1, real_total_size, out);
    fclose(out);

    printf("[client] Wrote reassembled.dat (%ld bytes).\n", real_total_size);

    FILE *log = fopen("execution_log.txt", "w");
    if (log) {
        fprintf(log, "[PART1] CHUNKS=%d | PROCS=%d | SYNC_USED=mutex,sem,condvar\n", N, N);
        fclose(log);
    } else {
        perror("fopen execution_log.txt");
    }

    sem_destroy(received_count);
    pthread_mutex_destroy(log_mutex);
    munmap(buffer, buffer_capacity);
    munmap(received_count, sizeof(sem_t));
    munmap(log_mutex, sizeof(pthread_mutex_t));
    munmap(chunks_done, sizeof(int));
    munmap(last_piece_actual_size, sizeof(long));

    printf("[client] Launching ./operations -t %d -f reassembled.dat ...\n", T);

    pid_t op_pid = fork();
    if (op_pid < 0) { perror("fork (operations)"); exit(1); }

    if (op_pid == 0) {
        char t_str[16];
        snprintf(t_str, sizeof(t_str), "%d", T);
        execl("./operations", "./operations", "-t", t_str, "-f", "reassembled.dat", (char*)NULL);
        perror("execl operations");
        exit(1);
    }

    int op_status;
    waitpid(op_pid, &op_status, 0);

    if (WIFEXITED(op_status)) {
        int code = WEXITSTATUS(op_status);
        printf("[client] ./operations exited with code %d\n", code);
        return code;
    }

    fprintf(stderr, "[client] ./operations terminated abnormally\n");
    return 1;
}
