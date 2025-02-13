#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NTD 100

/**
 * Exercises malloc and free a fixed number of times, varying the block size.
 *
 * @param arg An integer representing the thread number
 */
void *
thread_start(void *arg)
{
	int id = (int)(intptr_t)arg;
	unsigned int seed = id;

	for (int mallocs = 0; mallocs < 200; mallocs++) {
		char *old_ptr = malloc(rand_r(&seed) % 32768);
		if (old_ptr == NULL) {
			perror("malloc");
			pthread_exit((void *)1);
		}
		for (int counter = 0; counter < 5000; counter++) {
			char *new_ptr = malloc(rand_r(&seed) % 32768);
			if (new_ptr == NULL) {
				perror("malloc");
				pthread_exit((void *)1);
			}
			free(old_ptr);
			old_ptr = new_ptr;
		}
		free(old_ptr);
		printf("%d", id);
		fflush(stdout);
	}
	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t td[NTD];
	int ntd, rc;

	if (argc == 1)
		ntd = 5;
	else if (argc == 2) {
		ntd = atoi(argv[1]);
		if (ntd > NTD) {
			ntd = 5;
			fprintf(stderr, "Maximum thread count exceeded; "
			    "capping thread count at 5\n");
		}
	} else {
		fprintf(stderr, "Usage: %s [<thread count>]\n", argv[0]);
		exit(1);
	}
	for (int i = 0; i < ntd; i++) {
		if ((rc = pthread_create(&td[i], NULL, thread_start,
		    (void *)(intptr_t)i)) != 0) {
			fprintf(stderr, "pthread_create: %s\n", strerror(rc));
			exit(1);
		}
	}
	for (int i = 0; i < ntd; i++) {
		if ((rc = pthread_join(td[i], NULL)) != 0) {
			fprintf(stderr, "pthread_join: %s\n", strerror(rc));
			exit(1);
		}
	}
	printf("\n");
}
