#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

// --- Global variables  for min/max calculations ---
int global_min = INT_MAX;
int global_max = INT_MIN;
pthread_mutex_t lock;
int *dataset;
int total_numbers = 0;

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

int main(int argc,char *argv[]) {
int num_threads = 1;
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

//Allocate memory for threads and their data
pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
ThreadData *thread_data = malloc(num_threads * sizeof(ThreadData));

printf("\nSpawning workers...\n");

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

free(dataset);
pthread_mutex_destroy(&lock);

free(threads);
free(thread_data);

return 0;
}
