#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#include "smalloc.h"

#define PAGE_SIZE 4096UL
#define MIN_ALIGNMENT 32
#define ALIGNMENT_BITS 5

#define ARR_SIZE(_x) (sizeof(_x) / sizeof((_x)[0]))

static inline uintptr_t align_to(uintptr_t addr, size_t align) {
	return addr & ~(align - 1);
}

static inline uintptr_t align_up(uintptr_t addr, size_t align) {
	return (addr + (align - 1)) & ~(align - 1);
}

static inline uintptr_t page_align(uintptr_t addr) {
	return align_to(addr, PAGE_SIZE);
}

static inline uintptr_t page_offset(uintptr_t addr) {
	return addr & (PAGE_SIZE - 1);
}

static inline int page_aligned(uintptr_t addr) {
	return page_offset(addr) == 0;
}

/* A structure managing a user data page in blocks of a determined
   size (bsize) */
struct _node_t {
	struct _node_t* next;
	void* page;
	uint32_t idx;
	uint16_t bsize;
};

static void node_init(node_t* node, void* mem, size_t bsize,
	uint32_t idx)
{
	size_t i;
	uint16_t* ptr;

	bsize = align_up(bsize, MIN_ALIGNMENT);
	node->page = mem;
	node->next = NULL;
	node->bsize = bsize;
	node->idx = idx;

	/* Set up the free list */
	for (i = 0; i < PAGE_SIZE; i += bsize) {
		ptr = (uint16_t*)((uintptr_t)node->page + i);
		*ptr = i + bsize;
	}
}

static inline void node_set_full(node_t* node) {
	node->page = (void*)((uintptr_t)node->page | 1);
}

static inline int node_is_full(const node_t* node) {
	return ((uintptr_t)node->page & 1) != 0;
}

static void node_set_off(node_t* node, uint16_t off) {
	uintptr_t page = page_align((uintptr_t)node->page);
	node->page = (void*)(page | off);
}

// Allocate a chunk of memory from the given node, if there is room.
static void* node_alloc(node_t* node) {
	uint16_t* ptr;
	uint16_t next_off;

	if (node_is_full(node))
		return NULL;

	ptr = (uint16_t*)node->page;
	next_off = *ptr;

	if (next_off + node->bsize >= PAGE_SIZE - 1)
		node_set_full(node);
	else
		node_set_off(node, next_off);
	return (void*)ptr;
}

// Free a chunk of memory from the given node, if it belongs to it.
static int node_free(node_t* node, void* uptr) {
	const uintptr_t page = (uintptr_t)node->page;
	const uintptr_t ptr = (uintptr_t)uptr;

	/* Check if this pointer belongs to this node */
	if (page_align(page) != page_align(ptr))
		return 1;

	/* Check pointer alignment */
	assert(align_to(ptr, node->bsize) == ptr);

	*(uint16_t*)ptr = page_offset(page);
	node_set_off(node, page_offset(ptr));
	return 0;
}

// Initialize a slab with the given block size
static int slab_init(slab_t* slab, pa_t* pa, size_t bsize) {
	node_t* node;
	void* page;

	assert(bsize >= MIN_ALIGNMENT);

	node = (node_t*)pa->alloc_page(pa->opaque);
	page = pa->alloc_page(pa->opaque);
	if (!node || !page)
		return 1;
	node_init(node, page, bsize, 0);

	slab->head = node;
	slab->lowest_free = node;
	slab->pa = pa;
	return 0;
}

static node_t* slab_next_node(slab_t* slab, node_t* node) {
	const uintptr_t page = page_align((uintptr_t)node);
	const uintptr_t end = (uintptr_t)node + sizeof(*node);
	const uintptr_t next_start = align_up(end, 8);
	const uintptr_t next_end = next_start + sizeof(*node);
	pa_t* const pa = slab->pa;
	void* mem;
	node_t* new_node;

	/* Get the user data page */
	mem = pa->alloc_page(pa->opaque);
	if (!mem)
		return NULL;

	/* Fit the node in the current node page if there is room,
	   otherwise request a new one */
	if (page_align(next_end) == page) {
		new_node = (node_t*)next_start;
	} else {
		new_node = (node_t*)pa->alloc_page(pa->opaque);
		if (!new_node) {
			pa->free_page(pa->opaque, mem);
			return NULL;
		}
	}

	node_init(new_node, mem, node->bsize, node->idx + 1);
	return new_node;
}

static void* slab_alloc(slab_t* slab, node_t** outnode) {
	node_t* node = slab->lowest_free;
	void* out;

	do {
		/* Try to allocate from the current node, otherwise try with
		   the next one */
		out = node_alloc(node);
		if (out) {
			*outnode = node;
			slab->lowest_free = node;
			return out;
		}

		if (!node->next)
			node->next = slab_next_node(slab, node);

		node = node->next;
	} while (node);

	return NULL;
}

static void slab_free(slab_t* slab, void* ptr, node_t* node) {
	if (node->idx < slab->lowest_free->idx)
		slab->lowest_free = node;

	/* If this fails the allocation metadata likely got corrupted */
	assert(node_free(node, ptr) == 0);
}

static void slab_release(slab_t* slab) {
	node_t* node = slab->head;
	pa_t* const pa = slab->pa;
	node_t* last_page_head = NULL;
	void* user_page;

	if (!pa->free_page)
		return;

	while (node != NULL) {
		/* First free the user page */
		user_page = (void*)page_align((uintptr_t)node->page);
		pa->free_page(pa->opaque, user_page);

		/* When we find the beginning of a slab page, save it and
		   release it when we step on the next one */
		if (page_align((uintptr_t)node) == (uintptr_t)node) {
			if (last_page_head)
				pa->free_page(pa->opaque, last_page_head);
			last_page_head = node;
		}
		node = node->next;
	}
	if (last_page_head)
		pa->free_page(pa->opaque, last_page_head);

	slab->head = NULL;
	slab->lowest_free = NULL;
}

/* The header prepended to allocated chunks */
typedef struct {
	node_t* node;
	unsigned char user[];
} allocation_t;

static allocation_t* user2alloc(void* ptr) {
	return (allocation_t*)((uintptr_t)ptr - offsetof(allocation_t, user));
}

static size_t size2idx(size_t len) {
	return 63 - __lzcnt64((len - 1) >> ALIGNMENT_BITS) + 1;
}

/* The rounded allocation size for a user-supplied length */
static inline size_t round_alloc_size(size_t len) {
	size_t rlen;

	rlen = align_up(sizeof(allocation_t) + len, MIN_ALIGNMENT);
	if (size2idx(rlen) < ARR_SIZE(((smalloc_t*)0)->slabs))
		return rlen;

	rlen = align_up(len, PAGE_SIZE);
	if (rlen / PAGE_SIZE == 1)
		return PAGE_SIZE;

	return SIZE_MAX;
}

/* The size of an existing allocation, from the given user pointer */
static inline size_t userptr_alloc_size(void* userptr) {
	if (page_aligned((uintptr_t)userptr))
		return PAGE_SIZE;

	return user2alloc(userptr)->node->bsize;
}

int smalloc_init(smalloc_t* alloc, const pa_t* pa) {
	int res = 0;
	size_t i = 0;

	alloc->pa = *pa;
	for (i = 0; i < ARR_SIZE(alloc->slabs); ++i) {
		res = slab_init(alloc->slabs + i, &alloc->pa,
		                MIN_ALIGNMENT << i);
		if (res)
			break;
	}

	if (res) {
		size_t j;
		for (j = 0; j < i; ++j)
			slab_release(alloc->slabs + j);
	}
	return res;
}

static void* smalloc_big_alloc(pa_t* pa, size_t len) {
	size_t num_pages = align_up(len, PAGE_SIZE) / PAGE_SIZE;

	if (num_pages == 1)
		return pa->alloc_page(pa->opaque);
	return NULL;
}

static void smalloc_big_free(pa_t* pa, void* userptr) {
	pa->free_page(pa->opaque, userptr);
}

void* smalloc_alloc(smalloc_t* sm, size_t len) {
	size_t rlen, idx;
	allocation_t* ptr;
	node_t* node;

	if (len == 0)
		return NULL;

	rlen = round_alloc_size(len);
	if (rlen == SIZE_MAX)
		return NULL;

	if (rlen == PAGE_SIZE)
		return smalloc_big_alloc(&sm->pa, len);

	idx = size2idx(rlen);
	ptr = (allocation_t*)slab_alloc(sm->slabs + idx, &node);
	if (!ptr)
		return NULL;

	ptr->node = node;
	return &ptr->user;
}

void* smalloc_realloc(smalloc_t* sm, void* oldptr, size_t len) {
	void* newptr;
	size_t old_len, rlen;

	if (oldptr == NULL)
		return smalloc_alloc(sm, len);

	if (len == 0) {
		smalloc_free(sm, oldptr);
		return NULL;
	}

	/* The rounded size for the new allocation */
	rlen = round_alloc_size(len);
	if (rlen == SIZE_MAX)
		return NULL;

	/* The size for the old allocation */
	old_len = userptr_alloc_size(oldptr);

	/* If the new allocation fits in the old slot, simply reuse it */
	if (rlen == old_len)
		return oldptr;

	/* Prepare the new block, copy data and free old block */
	newptr = smalloc_alloc(sm, rlen);
	if (newptr) {
		memcpy(newptr, oldptr, old_len < len ? old_len : len);
		smalloc_free(sm, oldptr);
	}

	return newptr;
}

void smalloc_free(smalloc_t* sm, void* userptr) {
	uint16_t idx;
	allocation_t* ptr;

	if (!userptr)
		return;

	if (page_aligned((uintptr_t)userptr)) {
		smalloc_big_free(&sm->pa, userptr);
		return;
	}

	ptr = user2alloc(userptr);
	idx = size2idx(ptr->node->bsize);
	if (idx < ARR_SIZE(sm->slabs))
		slab_free(sm->slabs + idx, ptr, ptr->node);
}

void smalloc_release(smalloc_t* sm) {
	size_t i;

	for (i = 0; i < ARR_SIZE(sm->slabs); ++i)
		slab_release(sm->slabs + i);
}
