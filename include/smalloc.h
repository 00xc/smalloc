#ifndef _SMALLOC_H
#define _SMALLOC_H

/* A user-provided page allocator */
typedef struct {
	void* opaque;
	void* (*alloc_page)(void*);
	void (*free_page)(void*, void*);
} pa_t;

/* These should NOT be used directly by library users */
typedef struct _node_t node_t;
typedef struct {
	node_t* head;
	node_t* lowest_free;
	pa_t* pa;
} slab_t;

/*
 * The memory allocator. Fields should NOT be accessed by library
 * users directly.
 */
typedef struct {
	slab_t slabs[8];
	pa_t pa;
} smalloc_t;

/* Initialize the fields of the allocator */
int smalloc_init(smalloc_t* alloc, const pa_t* pa);

/* Allocate a block of the specified size */
void* smalloc_alloc(smalloc_t* alloc, size_t len);

/* Reallocate a previously allocated block */
void* smalloc_realloc(smalloc_t* sm, void* oldptr, size_t len);

/* Free a previously allocated block */
void smalloc_free(smalloc_t* alloc, void* ptr);

/* Release resources back to the page allocator. `alloc` may not be
   used after calling this function */
void smalloc_release(smalloc_t* alloc);

#endif
