# A5: Dynamic Storage Allocator

My memory allocator implementation for CSC 252. 
I think I went overboard on the macros for this. 
They were quite useful for testing different policies though.

**Author:** Skylar Abruzese

[Source Code](mm.c)

[Original Assignment](https://github.com/labruzese/a5/edit/master/README.md#this-project-was-created-for-this-assignment)

# Table of Contents
- [A5: Dynamic Storage Allocator](#a5-dynamic-storage-allocator)
  - [General Design Decisions](#general-design-decisions)
  - [Block Structure](#block-structure)
    - [Allocated Block](#allocated-block)
    - [Free Block](#free-block)
  - [Policies](#policies)
    - [Split Policy](#split-policy)
    - [Reallocation Strategy](#reallocation-strategy)
    - [Search Strategy](#search-strategy)
    - [Allocation Strategy](#allocation-strategy)
    - [Init Policy](#init-policy)
  - [Memory Organization](#memory-organization)
  - [API](#api)
  - [Implementation Details](#implementation-details)
    - [Initialization](#initialization)
    - [Allocation](#allocation)
    - [Freeing](#freeing)
    - [Debugging](#debugging)
- [ORIGINAL ASSIGNMENT](#this-project-was-created-for-this-assignment)

## General Design Decisions

- **Segregated Free Lists**
- **Explicit Free Lists**
- **Boundary Tags**
- **Adaptive Allocation Policies**

## Block Structure
* Header: 8 bytes
* Footer: 8 bytes
* Pointers: 8 bytes

*pointers point to next_ptr or the beginning of payload (depending if the block is allocated)*

### Allocated Block
```
[header][payload][optional padding][footer]
```

### Free Block
```
[header][next_ptr][prev_ptr][...unused...][footer]
```

## Policies

### Split Policy
- `SPLIT_IF_REMAINDER_BIGGER_THAN`
	- Controls when to split a free block during allocation. If the remainder after allocation would be bigger than this value (set to MINIMUM_UNALLOC), the block is split.
- `SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC`
	- Similar to above but specifically for reallocation operations (set to CHUNKSIZE).
- `SPLIT_ON_REALLOC`
    - Determines whether to split blocks during reallocation when expanding or shrinking. Enabled by default.

### Reallocation Strategy
- Multiple approaches tried in sequence:
  1. Try to shrink in place
  2. Try to combine with next block
  3. Try to combine with previous block
  4. Try to combine with both blocks
  5. Allocate a new block if necessary
- `REALLOC_BUFFER`
    - When set to non-zero, requests additional space (size * REALLOC_BUFFER) beyond what was asked
    for during reallocation to reduce future reallocations. 0 will disable it, 1 will make it do
    nothing. Disabled by default.

### Search Strategy
- Searches appropriate size class first, then moves to larger classes if we haven't anything
- `FIT_SEARCH_DEPTH`
    - Controls the search strategy for finding a free block. The high value set (1 << 16) essentially
    implements a best-fit strategy. A value of 0 would implement first-fit. Best fit by default.

### Allocation Strategy
- `USE_ALT`
    - Alternates between allocating from the left side and right side of a free block every time
    the heap is extended. This is designed to counteract exploitative test cases. This will
    generally allocate large blocks and small blocks on different sides since large blocks enough
    blocks are guarenteed to extend the heap and smaller blocks are a lot less likely to. This is
    enabled by default.

- `LARGE_OBJECT_THRESHOLD`
    - When USE_ALT is not used, this allocates large objects (larger than the threshold) to the right
    side of a free block to reduce fragmentation. This doesn't work as well as USE_ALT. Enabled by
    default if USE_ALT is disabled.

### Init Policy

- `SMALL_INIT_SIZE` and `SMALL_INIT_AMT`
    - These control whether to dedicate the first chunk of memory during initialization to smaller
    chunks. By default, this is turned off (AMT = 0).


## Memory Organization

- The allocator uses segregated free lists (NUM_LISTS set to 16), dividing free blocks into different
size classes for more efficient allocation and deallocation.
- The allocator provides two implementations of the segregated lists: one using an array (when
USE_ARRAY is defined) and another using individual pointer variables (when USE_ARRAY is not
defined).

*Note that NUM_LISTS doesn't change anything if USE_ARRAY is disabled*

## API

The allocator implements the standard memory allocation API:

- `int mm_init(void)`: Initialize the memory manager
- `void *mm_malloc(size_t size)`: Allocate a block of at least `size` bytes
- `void mm_free(void *bp)`: Free a previously allocated block
- `void *mm_realloc(void *ptr, size_t size)`: Resize a previously allocated block

## Implementation Details


### Initialization
The allocator initializes by:
1. Creating an initial heap with prologue and epilogue blocks
2. Extending the heap with a free block of `CHUNKSIZE` bytes
3. Optionally pre-allocating smaller blocks if configured

### Allocation
The allocation process:
1. Adjust the requested size to include overhead and alignment
2. Find a suitable free block using the search strategy
3. If no fit is found, extend the heap
4. Place the allocated block in the free block, potentially splitting it

### Freeing
When freeing a block:
1. Mark the block as unallocated
2. Coalesce with adjacent free blocks if possible
3. Insert the resulting block into the appropriate free list

### Debugging
The implementation includes a `mm_check()` function that verifies heap consistency by checking:
1. All blocks in free lists are marked as free
2. No uncoalesced free blocks exist
3. All free blocks are in the appropriate free list
4. Free list pointers are valid


# This project was created for this assignment
##\################################################################### <br />
\# CS:APP Malloc Lab <br />
\# Handout files for students <br />
\# <br />
\# Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved. <br />
\# May not be used, modified, or copied without permission. <br />
\# <br />
##\####################################################################

### Main Files:

mm.{c,h}	<br />
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Your solution malloc package. mm.c is the file that you<br />
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;will be handing in, and is the only file you should modify.<br />

mdriver.c	<br />
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;The malloc driver that tests your mm.c file<br />

short{1,2}-bal.rep	<br />
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Two tiny tracefiles to help you get started. <br />

Makefile	<br />
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Builds the driver<br />


### Other support files for the driver

config.h&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Configures the malloc lab driver<br />
fsecs.{c,h}&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Wrapper function for the different timer packages<br />
clock.{c,h}&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Routines for accessing the Pentium and Alpha cycle counters<br />
fcyc.{c,h}&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Timer functions based on cycle counters<br />
ftimer.{c,h}&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Timer functions based on interval timers and gettimeofday()<br />
memlib.{c,h}&nbsp;&nbsp;&nbsp;Models the heap and sbrk function<br />


### Building and running the driver

To build the driver, type "make" to the shell.

To run the driver on a tiny test trace:

	unix> mdriver -V -f short1-bal.rep

The -V option prints out helpful tracing and summary information.

To get a list of the driver flags:

	unix> mdriver -h

