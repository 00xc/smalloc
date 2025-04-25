#define _POSIX_C_SOURCE 200112L
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "smalloc.h"

#define MAX_ACTIVE_ALLOCS 2048
#define NUM_ROUNDS 60000000
#define ALLOC_SIZE 64
#define NUM_RANDS 8192

#ifdef SMALLOC_EX_TRACE
#	define TRACE_ALLOC(__ptr) printf("a %p\n", (void*)__ptr)
#	define TRACE_FREE(__ptr) printf("f %p\n", (void*)__ptr)
#else
#	define TRACE_ALLOC(__ptr) do {} while(0)
#	define TRACE_FREE(__ptr) do {} while(0)
#endif

static unsigned char volatile* pointers[MAX_ACTIVE_ALLOCS] = {0};

static void* alloc_page(void* opaque) {
	void* page = NULL;

	(void)opaque;
	if (posix_memalign(&page, 4096, 4096))
		errx(EXIT_FAILURE, "posix_memalign() failed");

	return page;
}

static void free_page(void* opaque, void* page) {
	(void)opaque;
	free(page);
}

static pa_t pa = {
	.alloc_page = alloc_page,
	.free_page = free_page,
};

static int* gen_rands() {
	size_t i;
	int* rands;

	rands = malloc(NUM_RANDS * sizeof(int));
	if (!rands)
		return NULL;

	srand(666);
	for (i = 0; i < NUM_RANDS; ++i)
		rands[i] = rand();
	return rands;
}

static int get_rand(int* rands, size_t i) {
	return rands[i % NUM_RANDS];
}

static void run_glibc(int* rands) {
	size_t i, np = 0;

	for (i = 0; i < NUM_ROUNDS; ++i) {
		unsigned char volatile* ptr = malloc(ALLOC_SIZE);
		TRACE_ALLOC(ptr);
		ptr[0] = 1;
		pointers[np++] = ptr;

		if (np == MAX_ACTIVE_ALLOCS) {
			int r = (get_rand(rands, i) % MAX_ACTIVE_ALLOCS) + 1;
			for (int v = 1; v <= r; ++v) {
				TRACE_FREE((void*)pointers[MAX_ACTIVE_ALLOCS - v]);
				free((void*)pointers[MAX_ACTIVE_ALLOCS - v]);
				np--;
			}
		}
	}

	for (i = 0; i < np; ++i) {
		TRACE_FREE((void*)pointers[i]);
		free((void*)pointers[i]);
	}
}

static void run_smalloc(int* rands) {
	size_t i, np = 0;
	smalloc_t sm;

	if (smalloc_init(&sm, &pa))
		err(EXIT_FAILURE, "smalloc_init");

	for (i = 0; i < NUM_ROUNDS; ++i) {
		unsigned char volatile* ptr = smalloc_alloc(&sm, ALLOC_SIZE);
		TRACE_ALLOC(ptr);
		ptr[0] = 1;
		pointers[np++] = ptr;

		if (np == MAX_ACTIVE_ALLOCS) {
			int r = (get_rand(rands, i) % MAX_ACTIVE_ALLOCS) + 1;
			for (int v = 1; v <= r; ++v) {
				TRACE_FREE((void*)pointers[MAX_ACTIVE_ALLOCS - v]);
				smalloc_free(&sm, (void*)pointers[MAX_ACTIVE_ALLOCS - v]);
				np--;
			}
		}
	}

	for (i = 0; i < np; ++i) {
		TRACE_FREE((void*)pointers[i]);
		smalloc_free(&sm, (void*)pointers[i]);
	}

	smalloc_release(&sm);
}

static int usage(const char* name) {
	fprintf(stderr, "%s <smalloc | libc>\n", name);
	return EXIT_FAILURE;
}

static int parse_args(int argc, const char* argv[]) {
	if (argc < 2)
		return -1;

	if (!strcmp(argv[1], "libc"))
		return 0;

	if (!strcmp(argv[1], "smalloc"))
		return 1;

	return -1;
}

int main(int argc, const char* argv[]) {
	int* rands;
	int use_smalloc;

	use_smalloc = parse_args(argc, argv);
	if (use_smalloc < 0)
		return usage(argv[0]);

	rands = gen_rands();
	if (!rands)
		err(EXIT_FAILURE, "gen_rands()");

	if (use_smalloc)
		run_smalloc(rands);
	else
		run_glibc(rands);

	free(rands);

	return EXIT_SUCCESS;
}
