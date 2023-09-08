# smalloc

A slab-based memory allocator in ~350 SLOC.

## Features ##

This is a toy project, and thus it is not a production-ready memory allocator. However, here's an overview of what it offers:

* Pros:
	* Small and easy to understand - implemented in ~350 SLOC.
	* Pluggable - it only requires the user to implement a page allocator; this could be `mmap()` or `posix_memalign()` for userspace applications, or your own page table management system in ring 0.
	* Fast-ish - can outperform glibc for workloads where the application's memory consumption is bounded (see the [example program](src/example.c)) and thus most memory is reused from previously allocated pages.
* Cons:
	* Worse performance than glibc for certain workloads.
	* Does not handle allocations over the page size (4k).
	* Does not have an API for aligned allocations.
	* Single-threaded design.

## Building ##

The project does not have any external dependencies and should build on any modern POSIX-compliant x86-64 system. It can be built as a shared or static library:

```
$ make libsmalloc.so
$ make libsmalloc.a
```

This repository also includes an example application that uses either smalloc or the libc allocator with a custom workload. This can be used to benchmark both allocators and compare them:

```
$ make example
$ time ./example smalloc
$ time ./example libc
```

## Testing ##

The example program can be used to test the allocator. If compiled with the preprocessor `SMALLOC_EX_TRACE` constant defined, the application will log all allocations and frees. The generated trace can be checked with the aptly named [check.py](scripts/check.py) script to detect bugs such as double allocations or invalid pointers.

```
$ CFLAGS="-DSMALLOC_EX_TRACE" make example
$ ./example smalloc > trace.txt
$ ./scripts/check.py trace.txt
```

Keep in mind by default this will generate a trace of over 1GB, and the script will take a few minutes to run.

## Usage ##

The API is very simple and consists of 4 functions and 2 structures. Refer to the library header ([smalloc.h](include/smalloc.h)) or the example program ([example.c](src/example.c)) for further details.

## Design ## 

The design for the allocator is very simple. It consists of a series of slabs, each managing blocks of a different fixed size. Each new allocation is directed to the appropriate slab with the next bigger block size. To keep track of which allocation belongs to which slab, additional metadata is prepended to the returned allocated block.

Each slab manages a linked list of pages from which blocks are returned to the user. Each of these pages is divided in blocks of a size determined by the specific slab. The linked list metadata, such as the pointers to the pages, are stored in separate metadata pages.

```
[metadata page]
+-------------+
| slab node:  |
|   page >----+-----+
|   next >----+--+  |
|   ...       |  |  |
+-------------+<-+  |
| slab node   |     |
|   page      |     |
|   next >----+--+  |
|   ...       |  |  |
+-------------+<-+  |
|   ...       |     |
+-------------+     |
                    |
[user data page]    |
+-------------+ <---+
| ...         |
+-------------+
```

Each node manages an inline free list of blocks on each page. The page pointer points to the head of the free list (instead of to the beginning of the page). Allocating will result in popping an element from the head of the free list, and freeing will push that element back to the front of the list.

```
[metadata page]
+-------------+
| slab node:  |
|   page >----+---------+
|   ...       |         |
+-------------+         |
| ...         |         |
+-------------+         |
                        |
[user data page]        |
+------------------+    |
| 0: allocated     |    |
+------------------+ <--+
| 1: free (next=4) | >--+
+------------------+    |
| 2: allocated     |    |
+------------------+    |
| 3: allocated     |    |
+------------------+ <--+
| 4: free (next=n) | >--+
+------------------+    |
| ...              |  ...
```

As mentioned above, the allocator prepends some metadata in the block returned by the slab. This prepended data is in fact a pointer to the node that owns the allocated block.

```
    [metadata page]
    +-------------+
+-> | slab node:  |
|   |   page >----+---------+
|   |   ...       |         |
|   +-------------+         |
|   | ...         |         |
|   +-------------+         |
|                           |
|   [user data page]        |
|   +------------------+    |
+---+--<    node       |    |
|   |  --------------  |    |
|   |    [user data]   |    |
|   +------------------+ <--+
|   |   free (next=n)  |
|   +------------------+
+---+--<    node       |
    |  --------------  |
    |    [user data]   |
    +------------------+
```