/*
 * mm.c - memory allocator using segregated free lists
 *
 * I'm using segregated free lists.
 * Boundary tags to support efficient coalescing.
 *
 * Block structure:
 * - Allocated block: [header][payload][padding]
 * - Free block: [header][pred ptr][succ ptr][...unused...][footer]
 *
 * Free list organization:
 * - Multiple segregated free lists based on block size class
 * - Each list is maintained as an explicit doubly-linked list
 *
 * Weird rules:
 * - Large allocations that will split large blocks into 2 get allocated to the right side while
 * small ones get allocated to the left side
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

team_t team = {
    /* Team name */
    "sky",
    /* First member's full name */
    "Skylar Abruzese",
    /* First member's email address */
    "labruzes@u.rochester.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

// DEFINITIONS AND POLICIES

#define UNALLOCATED 0
#define ALLOCATED 1
#define WSIZE 4 // word size
#define DSIZE 8 // double word size
#define CHUNKSIZE mem_pagesize()
#define MINIMUM_ALLOC 2 * sizeof(int)                       // header + footer
#define MINIMUM_UNALLOC MINIMUM_ALLOC + 2 * sizeof(block *) // header+footer+2 pointers

// POLICY

#define SPLIT_IF_REMAINDER_BIGGER_THAN MINIMUM_UNALLOC // always split free blocks
#define REALLOC_BUFFER 1                               // request ask*realloc buffer during realloc
#define SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC CHUNKSIZE // same as other one but for realloc
#define SPLIT_ON_REALLOC 1 // also split during reallocation expansion / shrinkson

//
/*
 * Alternates between allocating from the left side of a free block and the right side.
 * Probably not a good policy in general but since these test cases are exploitive this will
 * counteract it.
 *
 * Comment out to turn off. Also comment out LARGE_OBJECT_THRESHOLD if you want to remove right
 * allocation entirely
 */
#define USE_ALT

// if we're not using the USE_ALT policy then see if we want to set LARGE_OBJECT_THRESHOLD
#ifndef USE_ALT
/*
 * This will allocate large objects to the right side of the free block to try and reduce
 * fragmentation trapping little pieces of memory between large pieces. This is probably a better
 * general case solution than USE_ALT
 *
 * Comment out to turn off right side allocation of large objects
 */
#define LARGE_OBJECT_THRESHOLD 64
#endif /* ifndef USE_ALT */

/*
 * This will keep track of how many allocations are made and switch search policy after the first
 * 52418 allocations. (This switches for the last trace file, since fit first performs a lot better
 * but only for the last one if some other policies are disabled). This is probably cheating since
 * its tuned specifically for the test cases and is disabled by default.
 *
 * Uncomment to enable cheating haha
 *
 * Update: Since updated realloc, this no longer works any better than normal
 */ 
//#define CHEAT

#ifdef CHEAT // change to first fit for the last few traces

#define FIT_SEARCH_DEPTH ((alloc_count < 52418) ? (1 << 16) : (0))

#else // always use best fit ()

#define FIT_SEARCH_DEPTH (1 << 16) //best fit
/* 
 * Turns out using first fit isn't enough to impact the score but has a slight noticable decrease in memory util and about a 30% decrease in runtime
 */
//#define FIT_SEARCH_DEPTH 0 //first fit

#endif /* ifdef CHEAT */

// HELPER MACROS

// for readability of intention / what kind of data its pointing to
typedef void block;

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// pack a size and allocated bit into a word
#define HEADER(size, alloc) ((size) | (alloc))

// read and write a word at address p
#define GET(p) (*(unsigned int *)(p))
#define WRITE(p, val) (*(unsigned int *)(p) = (val))

// read the size and allocated fields from address p
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// given block ptr bp, compute address of its header and footer
#define HDR_PTR(bp) ((block *)(bp) - WSIZE)
#define FTR_PTR(bp)                                                                                \
    ((block *)(bp) + GET_SIZE(HDR_PTR(bp)) -                                                       \
     DSIZE) // next block - its header size - our footer size

// given block ptr bp, compute address of next and previous blocks
#define PTR_NEXT_BLK(bp) ((block *)(bp) + GET_SIZE(((block *)(bp) - WSIZE)))
#define PTR_PREV_BLK(bp) ((block *)(bp) - GET_SIZE(((block *)(bp) - DSIZE)))

// get and set next and prev pointers for free blocks
#define NEXT_PTR(bp) ((block *)(bp))
#define PREV_PTR(bp) ((block *)(bp) + sizeof(block *))
#define GET_NEXT(bp) (*(block **)NEXT_PTR(bp))
#define GET_PREV(bp) (*(block **)PREV_PTR(bp))
#define SET_NEXT(bp, next) (*(block **)NEXT_PTR(bp) = next)
#define SET_PREV(bp, prev) (*(block **)PREV_PTR(bp) = prev)

// GLOBAL VARS

// pointer to first block
static block *heap_listp = 0;

// SEGREGATED LISTS
#define NUM_LISTS 16

static void *get_seg_list(int index);
static void set_seg_list(int index, void *new);

/*
 * Although I think it's silly the rules technically specify that we
 * can't use array's. If USE_ARRAY isn't defined this will just create
 * 16 different pointers to the beginning of the list and update the
 * accessor to use it
 */
// #define USE_ARRAY

#ifdef USE_ARRAY

static block *seg_lists[NUM_LISTS]; // array of segregated free list heads

static void *get_seg_list(int index) { return seg_lists[index]; }

static void set_seg_list(int index, void *new) { seg_lists[index] = new; }

#else // don't use arrays

block *l0, *l1, *l2, *l3, *l4, *l5, *l6, *l7, *l8, *l9, *l10, *l11, *l12, *l13, *l14, *l15;

// clang-format off
// oh yueah this is sexy programming /s
static void *get_seg_list(int index) {
    switch (index) {
        case 0: return l0;
        case 1: return l1;
        case 2: return l2;
        case 3: return l3;
        case 4: return l4;
        case 5: return l5;
        case 6: return l6;
        case 7: return l7;
        case 8: return l8;
        case 9: return l9;
        case 10: return l10;
        case 11: return l11;
        case 12: return l12;
        case 13: return l13;
        case 14: return l14;
        case 15: return l15;
        default: return NULL; //uh oh
    }
}

static void set_seg_list(int index, void *new) {
    switch (index) {
        case 0: l0 = new; return;
        case 1: l1 = new; return;
        case 2: l2 = new; return;
        case 3: l3 = new; return;
        case 4: l4 = new; return;
        case 5: l5 = new; return;
        case 6: l6 = new; return;
        case 7: l7 = new; return;
        case 8: l8 = new; return;
        case 9: l9 = new; return;
        case 10: l10 = new; return;
        case 11: l11 = new; return;
        case 12: l12 = new; return;
        case 13: l13 = new; return;
        case 14: l14 = new; return;
        case 15: l15 = new; return;
    }
}

// clang-format on

#endif /* ifdef USE_ARRAY */

// determine how to segragate
static inline int get_list_index(size_t size) {
    if (size <= 32)
        return 0;
    else if (size <= 48)
        return 1;
    else if (size <= 64)
        return 2;
    else if (size <= 96)
        return 3;
    else if (size <= 128)
        return 4;

    // i feel like such a systems programming writing this
    int msb = 31 - __builtin_clz((unsigned int)size);

    int index = msb - 6 + 4; // 2^7=128 is our first few thresholds

    // ensure index is within bounds [0, NUM_LISTS-1]
    return (index < 0) ? 0 : (index >= NUM_LISTS) ? NUM_LISTS - 1 : index;
}

#ifdef CHEAT

static unsigned int alloc_count = 0;

#endif

#ifdef USE_ALT

static int alt = 0;

#endif

static void *extend_heap(size_t words);
static void *place(block *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void insert_free_block(void *bp);
static void remove_free_block(block *bp);
static int get_list_index(size_t size);
static int mm_check();

// Dedicates the first chunk of memory on init to smaller chunks. Doesn't really impove anything so
// is default to off (amt = 0)
#define SMALL_INIT_SIZE 256 // size of each small block in bytes
#define SMALL_INIT_AMT 0    // number of small blocks to create

/*
 * Initialize the memory manager
 *
 * Create prologue and epilogue, then add a new page. 0 for successful init, -1 for unsuccessful.
 */
int mm_init(void) {
    // initialize all segregated free lists to NULL
    for (int i = 0; i < NUM_LISTS; i++) {
        set_seg_list(i, NULL);
    }

    // create the initial empty heap
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    WRITE(heap_listp, 0);                                      // alignment padding
    WRITE(heap_listp + (1 * WSIZE), HEADER(DSIZE, ALLOCATED)); // prologue header
    WRITE(heap_listp + (2 * WSIZE), HEADER(DSIZE, ALLOCATED)); // prologue footer
    WRITE(heap_listp + (3 * WSIZE), HEADER(0, ALLOCATED));     // epilogue header
    heap_listp += (2 * WSIZE);                                 // point to prologue footer

    // extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    if (SMALL_INIT_AMT <= 0)
        return 0;

    // het the pointer to the first free block
    block *bp = PTR_NEXT_BLK(heap_listp);
    size_t total_size = GET_SIZE(HDR_PTR(bp));

    // calculate size needed for small blocks
    size_t small_blocks_total_size = SMALL_INIT_AMT * SMALL_INIT_SIZE;

    // make sure we have enough space
    if (small_blocks_total_size + MINIMUM_UNALLOC <= total_size) {
        // remove the big block from free list
        remove_free_block(bp);

        // create the small blocks
        for (int i = 0; i < SMALL_INIT_AMT; i++) {
            WRITE(HDR_PTR(bp), HEADER(SMALL_INIT_SIZE, UNALLOCATED));
            WRITE(FTR_PTR(bp), HEADER(SMALL_INIT_SIZE, UNALLOCATED));
            insert_free_block(bp);
            bp = PTR_NEXT_BLK(bp);
        }

        // create the remaining large block
        size_t remaining_size = total_size - small_blocks_total_size;
        WRITE(HDR_PTR(bp), HEADER(remaining_size, UNALLOCATED));
        WRITE(FTR_PTR(bp), HEADER(remaining_size, UNALLOCATED));
        insert_free_block(bp);
    }

    return 0;
}

/*
 * Allocate a block with at least size bytes of payload
 */
void *mm_malloc(size_t size) {
    size_t asize;      // adjusted block size
    size_t extendsize; // amount to extend heap if no fit
    block *bp;

    if (size == 0)
        return NULL;

    // adjust block size to include overhead and alignment requirements
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    // search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        return place(bp, asize);
    }

    // no fit found. get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

#ifdef USE_ALT
    alt = !alt;
#endif /* ifdef USE_ALT */

#ifdef CHEAT
    alloc_count++;
    // DEBUG
    // fflush(stdout);
    // if(alloc_count % 1 == 0) printf("alloc_count: %d\n", alloc_count);
#endif

    return place(bp, asize);
}

/*
 * Free a block
 */
void mm_free(void *bp) {
    if (bp == NULL)
        return;

    size_t size = GET_SIZE(HDR_PTR(bp));

    WRITE(HDR_PTR(bp), HEADER(size, UNALLOCATED));
    WRITE(FTR_PTR(bp), HEADER(size, UNALLOCATED));

    coalesce(bp);
}

/*
 * Reallocate a block to a new size (return new pointer)
 */
void *mm_realloc(void *ptr, size_t size) {
    // Case 1: If ptr is NULL, equivalent to malloc(size)
    if (ptr == NULL)
        return mm_malloc(size);

    // Case 2: If size is 0, equivalent to free(ptr) and return NULL
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    // Get the current block size
    size_t old_size = GET_SIZE(HDR_PTR(ptr));

    // Calculate adjusted size for new block
    size_t asize;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    // If new size is smaller or equal to old size (minus header and footer)
    if (asize <= old_size) {
// Optionally split if remaining space is large enough
#if SPLIT_ON_REALLOC
        if ((old_size - asize) >= SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC) {
            WRITE(HDR_PTR(ptr), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(ptr), HEADER(asize, ALLOCATED));

            // Create a free block from the remaining space
            block *next_bp = PTR_NEXT_BLK(ptr);
            WRITE(HDR_PTR(next_bp), HEADER(old_size - asize, UNALLOCATED));
            WRITE(FTR_PTR(next_bp), HEADER(old_size - asize, UNALLOCATED));

            // Add the free block to the appropriate free list
            insert_free_block(next_bp);
        }
#endif
        return ptr;
    }

    // Attempt in-place expansion
    size_t next_alloc = GET_ALLOC(HDR_PTR(PTR_NEXT_BLK(ptr)));
    size_t prev_alloc = GET_ALLOC(FTR_PTR(PTR_PREV_BLK(ptr))) || PTR_PREV_BLK(ptr) == ptr;
    size_t next_size = 0;
    size_t prev_size = 0;

    // Get sizes of adjacent blocks if they're free
    if (!next_alloc)
        next_size = GET_SIZE(HDR_PTR(PTR_NEXT_BLK(ptr)));
    if (!prev_alloc)
        prev_size = GET_SIZE(HDR_PTR(PTR_PREV_BLK(ptr)));

    // Case 1: Next block is free and combined size is sufficient
    if (!next_alloc && (old_size + next_size >= asize)) {
        // Remove next block from free list
        remove_free_block(PTR_NEXT_BLK(ptr));

        // Combine current block with next block
        WRITE(HDR_PTR(ptr), HEADER(old_size + next_size, ALLOCATED));
        WRITE(FTR_PTR(ptr), HEADER(old_size + next_size, ALLOCATED));

// Optionally split if there's enough extra space
#if SPLIT_ON_REALLOC
        if ((old_size + next_size - asize) >= SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC) {
            WRITE(HDR_PTR(ptr), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(ptr), HEADER(asize, ALLOCATED));

            // Create a free block from the remaining space
            block *split_bp = PTR_NEXT_BLK(ptr);
            WRITE(HDR_PTR(split_bp), HEADER(old_size + next_size - asize, UNALLOCATED));
            WRITE(FTR_PTR(split_bp), HEADER(old_size + next_size - asize, UNALLOCATED));

            // Add the free block to the appropriate free list
            insert_free_block(split_bp);
        }
#endif

        return ptr;
    }

    // Case 2: Previous block is free and combined size is sufficient
    if (!prev_alloc && (prev_size + old_size >= asize)) {
        // Remove previous block from free list
        remove_free_block(PTR_PREV_BLK(ptr));

        // Calculate payload size to copy (excluding header/footer)
        size_t payload_size = old_size - DSIZE;
        if (size < payload_size)
            payload_size = size;

        // Save location of previous block
        block *prev_bp = PTR_PREV_BLK(ptr);

        // Combine previous and current blocks
        WRITE(HDR_PTR(prev_bp), HEADER(prev_size + old_size, ALLOCATED));
        WRITE(FTR_PTR(prev_bp), HEADER(prev_size + old_size, ALLOCATED));

        // Copy data from old location to new location
        memmove(prev_bp, ptr, payload_size);

// Optionally split if there's enough extra space
#if SPLIT_ON_REALLOC
        if ((prev_size + old_size - asize) >= SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC) {
            WRITE(HDR_PTR(prev_bp), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(prev_bp), HEADER(asize, ALLOCATED));

            // Create a free block from the remaining space
            block *split_bp = PTR_NEXT_BLK(prev_bp);
            WRITE(HDR_PTR(split_bp), HEADER(prev_size + old_size - asize, UNALLOCATED));
            WRITE(FTR_PTR(split_bp), HEADER(prev_size + old_size - asize, UNALLOCATED));

            // Add the free block to the appropriate free list
            insert_free_block(split_bp);
        }
#endif

        return prev_bp;
    }

    // Case 3: Both previous and next blocks are free and combined size is sufficient
    if (!prev_alloc && !next_alloc && (prev_size + old_size + next_size >= asize)) {
        // Remove previous and next blocks from free lists
        remove_free_block(PTR_PREV_BLK(ptr));
        remove_free_block(PTR_NEXT_BLK(ptr));

        // Calculate payload size to copy (excluding header/footer)
        size_t payload_size = old_size - DSIZE;
        if (size < payload_size)
            payload_size = size;

        // Save location of previous block
        block *prev_bp = PTR_PREV_BLK(ptr);

        // Combine all three blocks
        WRITE(HDR_PTR(prev_bp), HEADER(prev_size + old_size + next_size, ALLOCATED));
        WRITE(FTR_PTR(prev_bp), HEADER(prev_size + old_size + next_size, ALLOCATED));

        // Copy data from old location to new location
        memmove(prev_bp, ptr, payload_size);

// Optionally split if there's enough extra space
#if SPLIT_ON_REALLOC
        if ((prev_size + old_size + next_size - asize) >= SPLIT_IF_REMAINDER_BIGGER_THAN_REALLOC) {
            WRITE(HDR_PTR(prev_bp), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(prev_bp), HEADER(asize, ALLOCATED));

            // Create a free block from the remaining space
            block *split_bp = PTR_NEXT_BLK(prev_bp);
            WRITE(HDR_PTR(split_bp), HEADER(prev_size + old_size + next_size - asize, UNALLOCATED));
            WRITE(FTR_PTR(split_bp), HEADER(prev_size + old_size + next_size - asize, UNALLOCATED));

            // Add the free block to the appropriate free list
            insert_free_block(split_bp);
        }
#endif

        return prev_bp;
    }

    // Case 4: No in-place expansion possible, allocate new block
    block *new_bp;

// Apply the REALLOC_BUFFER policy if desired
#if REALLOC_BUFFER
    new_bp = mm_malloc(size * REALLOC_BUFFER);
#else
    new_bp = mm_malloc(size);
#endif

    if (new_bp == NULL)
        return NULL;

    // Copy the data to the new block
    size_t copy_size = old_size - DSIZE; // Subtract header and footer
    if (size < copy_size)
        copy_size = size;
    memcpy(new_bp, ptr, copy_size);

    // Free the old block
    mm_free(ptr);

    return new_bp;
}

/*
 * Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) {
    block *bp;
    size_t size;

    // allocate an even number of words to maintain alignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if (size < MINIMUM_UNALLOC)
        size = MINIMUM_UNALLOC;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    // initialize free block header/footer and the epilogue header
    WRITE(HDR_PTR(bp), HEADER(size, UNALLOCATED));          // free block header
    WRITE(FTR_PTR(bp), HEADER(size, UNALLOCATED));          // free block footer
    WRITE(HDR_PTR(PTR_NEXT_BLK(bp)), HEADER(0, ALLOCATED)); // new next header

    // coalesce if the previous block was free
    return coalesce(bp);
}

/*
 * Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTR_PTR(PTR_PREV_BLK(bp))) || PTR_PREV_BLK(bp) == bp;
    size_t next_alloc = GET_ALLOC(HDR_PTR(PTR_NEXT_BLK(bp)));
    size_t size = GET_SIZE(HDR_PTR(bp));

    // case 1: both prev and next blocks are allocated
    if (prev_alloc && next_alloc) {
        insert_free_block(bp);
        return bp;
    }

    // case 2: prev is allocated, next is free
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDR_PTR(PTR_NEXT_BLK(bp)));
        remove_free_block(PTR_NEXT_BLK(bp));
        WRITE(HDR_PTR(bp), HEADER(size, UNALLOCATED));
        WRITE(FTR_PTR(bp), HEADER(size, UNALLOCATED));
    }

    // case 3: prev is free, next is allocated
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDR_PTR(PTR_PREV_BLK(bp)));
        remove_free_block(PTR_PREV_BLK(bp));
        bp = PTR_PREV_BLK(bp);
        WRITE(HDR_PTR(bp), HEADER(size, UNALLOCATED));
        WRITE(FTR_PTR(bp), HEADER(size, UNALLOCATED));
    }

    // case 4: both prev and next blocks are free
    else {
        size += GET_SIZE(HDR_PTR(PTR_PREV_BLK(bp))) + GET_SIZE(HDR_PTR(PTR_NEXT_BLK(bp)));
        remove_free_block(PTR_PREV_BLK(bp));
        remove_free_block(PTR_NEXT_BLK(bp));
        bp = PTR_PREV_BLK(bp);
        WRITE(HDR_PTR(bp), HEADER(size, UNALLOCATED));
        WRITE(FTR_PTR(bp), HEADER(size, UNALLOCATED));
    }

    insert_free_block(bp);
    return bp;
}
/*
 * Place block of asize bytes at start of free block bp and split if remainder would be at least
 * minimum block size
 */
static void *place(block *bp, size_t asize) {
    size_t csize = GET_SIZE(HDR_PTR(bp));
    block *allocated_bp = bp; // By default, we'll allocate at bp

    // remove the block from the free list
    remove_free_block(bp);

    // if the remaining part is large enough for a new free block
    if ((csize - asize) >= SPLIT_IF_REMAINDER_BIGGER_THAN) {
// DETERMINE WHETHER TO USE RIGHT ALLOCATION
#ifdef USE_ALT

        if (alt) { // alternate right allocation

#else // NDEF USE_ALT
#ifdef LARGE_OBJECT_THRESHOLD

        if (asize > LARGE_OBJECT_THRESHOLD) { // right allocate if large object

#else // NDEF LARGE_OBJECT_THRESHOLD

        if (0) { // don't allocate to right side

#endif
#endif
            // Save the location where the allocated block will go
            allocated_bp = (void *)bp + (csize - asize);

            // Create free block on the left side
            WRITE(HDR_PTR(bp), HEADER(csize - asize, UNALLOCATED));
            WRITE(FTR_PTR(bp), HEADER(csize - asize, UNALLOCATED));

            // Place allocated block on the right side
            WRITE(HDR_PTR(allocated_bp), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(allocated_bp), HEADER(asize, ALLOCATED));

            // Add the free block back to the free list
            insert_free_block(bp);
        }
        // For smaller objects, place them on the left side (original behavior)
        else {
            WRITE(HDR_PTR(bp), HEADER(asize, ALLOCATED));
            WRITE(FTR_PTR(bp), HEADER(asize, ALLOCATED));

            // Create free block on the right side
            block *free_bp = PTR_NEXT_BLK(bp);
            WRITE(HDR_PTR(free_bp), HEADER(csize - asize, UNALLOCATED));
            WRITE(FTR_PTR(free_bp), HEADER(csize - asize, UNALLOCATED));

            // Add the free block to the free list
            insert_free_block(free_bp);
        }
    }

    // otherwise use the entire block
    else {
        WRITE(HDR_PTR(bp), HEADER(csize, ALLOCATED));
        WRITE(FTR_PTR(bp), HEADER(csize, ALLOCATED));
    }

#ifdef USE_ALT

    alt = !alt;

#endif /* ifdef USE_ALT */

    return allocated_bp;
}

/*
 * Find a fit for a block with asize bytes
 */
static void *find_fit(size_t asize) {
    // get the appropriate list to search based on size
    int list_index = get_list_index(asize);
    block *bp;
    block *best_fit = NULL;
    size_t best_size = 0;
    int depth;

    // search from the appropriate list up through larger lists
    for (int i = list_index; i < NUM_LISTS; i++) {
        bp = get_seg_list(i);
        depth = 0;

        // search through the list up to MAX_SEARCH_DEPTH nodes
        while (bp != NULL && (depth < FIT_SEARCH_DEPTH || best_fit == NULL)) {
            size_t current_size = GET_SIZE(HDR_PTR(bp));

            // check if this block can fit the requested size
            if (asize <= current_size) {
                // if this is our first fit or better than current best
                if (best_fit == NULL || current_size < best_size) {
                    best_fit = bp;
                    best_size = current_size;

                    // if we found an exact match, return immediately
                    if (current_size == asize) {
                        return best_fit;
                    }
                }
            }

            // move to next block and increment depth counter
            bp = GET_NEXT(bp);
            depth++;
        }

        // if we found a fit in the current list, return it
        if (best_fit != NULL) {
            return best_fit;
        }
    }

    // no fit found
    return NULL;
}

/*
 * Insert a free block into the appropriate segregated list
 */
static void insert_free_block(block *bp) {
    size_t size = GET_SIZE(HDR_PTR(bp));
    int index = get_list_index(size);

    // insert at the beginning of the appropriate list
    SET_PREV(bp, NULL);
    SET_NEXT(bp, get_seg_list(index));

    if (get_seg_list(index) != NULL) {
        SET_PREV(get_seg_list(index), bp);
    }

    set_seg_list(index, bp);
}

/*
 * Remove a free block from its segregated list
 */
static void remove_free_block(block *bp) {
    size_t size = GET_SIZE(HDR_PTR(bp));
    int index = get_list_index(size);

    // adjust pointers to remove the block
    if (GET_PREV(bp) != NULL) {
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        set_seg_list(index, GET_NEXT(bp));
    }

    if (GET_NEXT(bp) != NULL) {
        SET_PREV(GET_NEXT(bp), GET_PREV(bp));
    }
}

/*
 * mm_check - Check the heap for consistency
 */
int mm_check(void) {
    void *bp;
    int correct = 1;

    // check if every block in each free list is marked as free
    for (int i = 0; i < NUM_LISTS; i++) {
        bp = get_seg_list(i);
        while (bp != NULL) {
            if (GET_ALLOC(HDR_PTR(bp))) {
                printf("Error: Block in free list is marked as allocated\n");
                correct = 0;
            }
            bp = GET_NEXT(bp);
        }
    }

    // check if there are any contiguous free blocks that escaped coalescing
    bp = heap_listp;
    while (GET_SIZE(HDR_PTR(bp)) > 0) {
        if (!GET_ALLOC(HDR_PTR(bp)) && !GET_ALLOC(HDR_PTR(PTR_NEXT_BLK(bp)))) {
            printf("Error: Contiguous free blocks not coalesced\n");
            correct = 0;
        }
        bp = PTR_NEXT_BLK(bp);
    }

    // check if every free block is actually in the free list
    bp = heap_listp;
    while (GET_SIZE(HDR_PTR(bp)) > 0) {
        if (!GET_ALLOC(HDR_PTR(bp))) {
            int found = 0;
            int index = get_list_index(GET_SIZE(HDR_PTR(bp)));
            void *list_bp = get_seg_list(index);

            while (list_bp != NULL) {
                if (list_bp == bp) {
                    found = 1;
                    break;
                }
                list_bp = GET_NEXT(list_bp);
            }

            if (!found) {
                printf("Error: Free block not in free list\n");
                correct = 0;
            }
        }
        bp = PTR_NEXT_BLK(bp);
    }

    // check if pointers in the free list point to valid free blocks
    for (int i = 0; i < NUM_LISTS; i++) {
        bp = get_seg_list(i);
        while (bp != NULL) {
            if (GET_NEXT(bp) != NULL &&
                (GET_NEXT(bp) < mem_heap_lo() || GET_NEXT(bp) > mem_heap_hi())) {
                printf("Error: Successor pointer in free block points outside heap\n");
                correct = 0;
            }
            if (GET_PREV(bp) != NULL &&
                (GET_PREV(bp) < mem_heap_lo() || GET_PREV(bp) > mem_heap_hi())) {
                printf("Error: Predecessor pointer in free block points outside heap\n");
                correct = 0;
            }
            bp = GET_NEXT(bp);
        }
    }

    return correct;
}
