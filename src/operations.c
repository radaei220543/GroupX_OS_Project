#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

// --- Global variables  for min/max calculations ---
int global_min = INT_MAX;
int global_max = INT_MIN;
pthread_mutex_t lock;
int *dataset;
int total_numbers = 0;
int num_threads = 1;

//structure to hold data for each thread 
typedef struct {
    int thread_id;
    int start_index;
    int end_index;
} ThreadData;

//The worker function that each thread will execute
void *worker_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    int local_min = INT_MAX;
    int local_max = INT_MIN;

    // Logic to find min/max in this chuck
    for (int i = data->start_index; i < data->end_index; i++) {
        if (dataset[i] < local_min) local_min = dataset[i];
        if (dataset[i] > local_max) local_max = dataset[i];
    }

    //Mutex lock to safely update global values
    pthread_mutex_lock(&lock);
    if (local_min < global_min) global_min = local_min;
    if (local_max > global_max) global_max = local_max;
    pthread_mutex_unlock(&lock);

    printf("Thread %d processed indices %d to %d (Local MIn: %d, Local Max: %d)\n",
    data->thread_id, data->start_index, data->end_index -1, local_min, local_max);

    pthread_exit(NULL);
}

// Sync primitives for task coordination (Hint 11.2.3)
pthread_mutex_t merge_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t merge_cond = PTHREAD_COND_INITIALIZER;
int active_merges = 0;

typedef struct {
    int start_index;
    int end_index;
} ThreadArgs;

int compare_ints(const void *a, const void *b) {
    return (*(int32_t*)a - *(int32_t*)b);
}

// --- DATA_PARALLEL: Initial sorting of contiguous blocks (Hint 11.2.1) ---
void *sort_worker(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    qsort(dataset + args->start_index, args->end_index - args->start_index + 1, sizeof(int32_t), compare_ints);
    free(args);
    return NULL;
}

void merge(int start, int mid, int end) {
    int32_t *temp = (int32_t *)malloc((end - start + 1) * sizeof(int32_t));
    int i = start, j = mid + 1, k = 0;
    while (i <= mid && j <= end) {
        if (dataset[i] <= dataset[j]) temp[k++] = dataset[i++];
        else temp[k++] = dataset[j++];
    }
    while (i <= mid) temp[k++] = dataset[i++];
    while (j <= end) temp[k++] = dataset[j++];
    for (i = start, k = 0; i <= end; i++, k++) dataset[i] = temp[k];
    free(temp);
}

// --- TASK_PARALLEL: Concurrent merge tasks coordinated via cond vars (Hint 11.2.2) ---
void *merge_task(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    merge(args->start_index, args->start_index + (args->end_index - args->start_index) / 2, args->end_index);
    free(args);
    pthread_mutex_lock(&merge_mutex);
    active_merges--;
    // Signal completion of current merge level (Hint 11.2.3)
    if (active_merges == 0) pthread_cond_signal(&merge_cond);
    pthread_mutex_unlock(&merge_mutex);
    return NULL;
}

int main(int argc,char *argv[]) {
    char *filename = NULL;
    int opt;

    //Parse command-line arguments
    while ((opt = getopt(argc, argv, "t:f:")) != -1) {
        switch (opt) {
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'f':
            filename = optarg;
            break;
        default:
            fprintf(stderr," Usage: %s -t <threads> -f <file>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    //Check if the file argument was provided
    if (filename == NULL) {
        fprintf(stderr, "Error:Target file (-f) is required.\n");
        exit(EXIT_FAILURE);
    }

    //Print confirmation to the terminal
    printf("--- Analytics Engine Started ---\n");
    printf("Threads allocated: %d\n", num_threads);
    printf("Target file: %s\n", filename);

    // --- READ THE DATA FILE ---
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Calculate total numbers from binary file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    total_numbers = file_size / sizeof(int);

    // Allocate memory and read all binary data at once
    dataset = malloc(total_numbers * sizeof(int));
    fread(dataset, sizeof(int), total_numbers, file);
    fclose(file);

    printf("--------------------------------\n");

    printf("Successfully loaded %d numbers from %s\n", total_numbers, filename);

    pthread_mutex_init(&lock, NULL);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    //Allocate memory for threads and their data
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    ThreadData *thread_data = malloc(num_threads * sizeof(ThreadData));

    printf("\nSpawning workers...\n");

    // --- DATA_PARALLEL: Min/Max reduction across contiguous blocks ---
    //Create the threads
    int chunk_size = total_numbers / num_threads;
    int current_index = 0;

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].start_index = current_index;
        //Last thread takes the remainder
        thread_data[i].end_index = (i == num_threads - 1) ? total_numbers : current_index + chunk_size;
        current_index = thread_data[i].end_index;

        pthread_create(&threads[i], NULL, worker_function, (void *)&thread_data[i]);
    }

    //wait for all threads to finish their work
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n--- Final Results ---\n");
    printf("Global Minimum: %d\n", global_min);
    printf("Global Maximum: %d\n", global_max);

    FILE *f_min = fopen("result_min.txt", "w");
    if (f_min != NULL) {
        fprintf(f_min, "%d\n", global_min);
        fclose(f_min);
    }

    FILE *f_max = fopen("result_max.txt", "w");
    if (f_max != NULL) {
        fprintf(f_max, "%d\n", global_max);
        fclose(f_max);
    }

    printf("--- Results saved to text files ---\n");

    // Phase 2: Data Parallelism - Independent subarray sorting
    int chunk = total_numbers / num_threads;
    for (int i = 0; i < num_threads; ++i) {
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->start_index = i * chunk;
        args->end_index = (i == num_threads - 1) ? (total_numbers - 1) : ((i + 1) * chunk - 1);
        pthread_create(&threads[i], NULL, sort_worker, args);
    }
    for (int i = 0; i < num_threads; ++i) pthread_join(threads[i], NULL);

    // Phase 3: Task Parallelism - Bottom-up parallel merging (Hint 11.2.3)
    for (int size = chunk; size < total_numbers; size *= 2) {
        pthread_mutex_lock(&merge_mutex);
        active_merges = 0;
        pthread_mutex_unlock(&merge_mutex);
        for (int i = 0; i < total_numbers - size; i += 2 * size) {
            ThreadArgs *args = malloc(sizeof(ThreadArgs));
            args->start_index = i;
            args->end_index = (i + 2 * size - 1 < total_numbers - 1) ? (i + 2 * size - 1) : (total_numbers - 1);
            pthread_mutex_lock(&merge_mutex);
            active_merges++;
            pthread_mutex_unlock(&merge_mutex);
            pthread_t t;
            pthread_create(&t, NULL, merge_task, args);
            pthread_detach(t); // Detach as we use cond_wait to sync the level
        }
        // Coordinate merge levels via condition variables (Hint 11.2.3)
        pthread_mutex_lock(&merge_mutex);
        while (active_merges > 0) pthread_cond_wait(&merge_cond, &merge_mutex);
        pthread_mutex_unlock(&merge_mutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    long time_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;

    // Output required results and logs (Section 6)
    FILE *out = fopen("result_sorted.dat", "wb");
    fwrite(dataset, sizeof(int32_t), total_numbers, out);
    fclose(out);

    FILE *log = fopen("execution_log.txt", "a");
    fprintf(log, "[PART2] THREADS=%d | DATA_PARALLEL=min,max | TASK_PARALLEL=sort\n", num_threads);
    fprintf(log, "[PART2] TIME_MS=%ld | SORT_ALGO=parallel_merge_sort\n[STATUS] SUCCESS\n", time_ms);
    fclose(log);

    free(dataset);
    pthread_mutex_destroy(&lock);

    free(threads);
    free(thread_data);

    return 0;
}
