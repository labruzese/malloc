/*
 * mm.c - Dynamic memory allocator using segregated free lists
 *
 * This implementation uses segregated free lists to efficiently manage memory.
 * Each size class has its own free list, which helps to reduce fragmentation
 * and improve allocation performance. The allocator uses an explicit free list
 * with boundary tags to support efficient coalescing.
 *
 * Block structure:
 * - Allocated block: [header][payload][padding]
 * - Free block: [header][pred ptr][succ ptr][...unused...][footer]
 *
 * Free list organization:
 * - Multiple segregated free lists based on block size class
 * - Each list is maintained as an explicit doubly-linked list
 * - Lists are sorted by address to improve coalescing efficiency
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
    "DynamicMemory",
    /* First member's full name */
    "Skylar Abruzese",
    /* First member's email address */
    "labruzes@u.rochester.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

#define WSIZE 4                                             // word size
#define DSIZE 8                                             // double word size
#define CHUNKSIZE mem_pagesize()                            // initial heap size
#define MINIMUM_ALLOC 2 * sizeof(int)                       // minimum block size
#define MINIMUM_UNALLOC MINIMUM_ALLOC + 2 * sizeof(block *) // minimum block size
#define FIT_SEARCH_DEPTH 1 << 31 // maximum block searched before switching to first fit

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// pack a size and allocated bit into a word
#define PACK(size, alloc) ((size) | (alloc))

// read and write a word at address p
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// read the size and allocated fields from address p
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

// given block ptr bp, compute address of its header and footer
#define HDRP(bp) ((block *)(bp) - WSIZE)
#define FTRP(bp)                                                                                   \
    ((block *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // next block - its header size - our footer size

// given block ptr bp, compute address of next and previous blocks
#define NEXT_BLKP(bp) ((block *)(bp) + GET_SIZE(((block *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((block *)(bp) - GET_SIZE(((block *)(bp) - DSIZE)))

typedef void block;

// get and set next and prev pointers for free blocks
#define NEXT_PTR(bp) ((block *)(bp))
#define PREV_PTR(bp) ((block *)(bp) + sizeof(block *))
#define GET_NEXT(bp) (*(block **)NEXT_PTR(bp))
#define GET_PREV(bp) (*(block **)PREV_PTR(bp))
#define SET_NEXT(bp, next) (*(block **)NEXT_PTR(bp) = next)
#define SET_PREV(bp, prev) (*(block **)PREV_PTR(bp) = prev)

// Number of segregated free lists and their size thresholds
#define NUM_LISTS 16

// Optimized list index calculation using bit manipulation
static inline int get_list_index(size_t size) {
    // Handle minimum allocation size (typically 16 or 32 bytes)
    if (size <= 32)
        return 0;
    
    // Use leading zeros to find the most significant bit position
    // which effectively gives us log2(size) and maps to appropriate bucket
    int msb = 31 - __builtin_clz((unsigned int)size);
    
    // Fine-tune index based on the size range
    // This maps sizes to appropriate lists based on their magnitude
    int index = msb - 4;  // Subtract 4 because 2^5=32 is our first threshold
    
    // Ensure index is within bounds [0, NUM_LISTS-1]
    return (index < 0) ? 0 : (index >= NUM_LISTS) ? NUM_LISTS - 1 : index;
}

// global variables
static block *heap_listp = 0;       // pointer to first block
static block *seg_lists[NUM_LISTS]; // array of segregated free list heads

// function prototypes for internal helper routines
static void *extend_heap(size_t words);
static void place(block *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void insert_free_block(void *bp);
static void remove_free_block(block *bp);
static int get_list_index(size_t size);
static int mm_check();

/*
 * mm_init - Initialize the memory manager
 */
int mm_init(void) {
    // initialize all segregated free lists to NULL
    for (int i = 0; i < NUM_LISTS; i++) {
        seg_lists[i] = NULL;
    }

    // create the initial empty heap
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);                            // alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // epilogue header
    heap_listp += (2 * WSIZE);                     // point to prologue footer

    // extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
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
        place(bp, asize);
        return bp;
    }

    // no fit found. get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *bp) {
    if (bp == NULL)
        return;

    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
}

/*
 * mm_realloc - Reallocate a block to a new size
 */
void *mm_realloc(void *ptr, size_t size) {
    block *oldptr = ptr;
    block *newptr;
    size_t copySize;

    // if ptr is NULL, realloc should be equivalent to mm_malloc
    if (ptr == NULL)
        return mm_malloc(size);

    // if size is 0, realloc should be equivalent to mm_free
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    // check if we can expand the current block
    size_t oldsize = GET_SIZE(HDRP(oldptr));
    size_t asize;

    // adjust block size to include overhead and alignment requirements
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    // if the new size is smaller than the old size, we can just shrink
    if (asize <= oldsize) {
        // only split if the remaining chunk would be large enough
        if (oldsize - asize >= 2 * DSIZE) {
            PUT(HDRP(oldptr), PACK(asize, 1));
            PUT(FTRP(oldptr), PACK(asize, 1));
            block *next_bp = NEXT_BLKP(oldptr);
            PUT(HDRP(next_bp), PACK(oldsize - asize, 0));
            PUT(FTRP(next_bp), PACK(oldsize - asize, 0));
            insert_free_block(next_bp);
        }
        return oldptr;
    }

    // check if the next block is free and can be used for expansion
    block *next = NEXT_BLKP(oldptr);
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t next_size = GET_SIZE(HDRP(next));

    // if the next block is free and together they can fit the requested size
    if (!next_alloc && (oldsize + next_size >= asize)) {
        remove_free_block(next);
        PUT(HDRP(oldptr), PACK(oldsize + next_size, 1));
        PUT(FTRP(oldptr), PACK(oldsize + next_size, 1));
        return oldptr;
    }

    // otherwise, we need to allocate a new block
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    // copy the old data
    copySize = oldsize - DSIZE; // exclude header and footer
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);

    // free the old block
    mm_free(oldptr);

    return newptr;
}

/*
 * extend_heap - Extend heap with free block and return its block pointer
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
    PUT(HDRP(bp), PACK(size, 0));         // free block header
    PUT(FTRP(bp), PACK(size, 0));         // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header

    // coalesce if the previous block was free
    return coalesce(bp);
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // case 1: both prev and next blocks are allocated
    if (prev_alloc && next_alloc) {
        insert_free_block(bp);
        return bp;
    }

    // case 2: prev is allocated, next is free
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free_block(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    // case 3: prev is free, next is allocated
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    // case 4: both prev and next blocks are free
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_free_block(PREV_BLKP(bp));
        remove_free_block(NEXT_BLKP(bp));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    insert_free_block(bp);
    return bp;
}

/*
 * place - Place block of asize bytes at start of free block bp
 *         and split if remainder would be at least minimum block size
 */
static void place(block *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));

    // remove the block from the free list
    remove_free_block(bp);

    // if the remaining part is large enough for a new free block
    if ((csize - asize) >= MINIMUM_UNALLOC) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        insert_free_block(bp);
    }
    // otherwise use the entire block
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * find_fit - Find a fit for a block with asize bytes
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
        bp = seg_lists[i];
        depth = 0;

        // Search through the list up to MAX_SEARCH_DEPTH nodes
        while (bp != NULL && (depth < FIT_SEARCH_DEPTH || best_fit == NULL)) {
            size_t current_size = GET_SIZE(HDRP(bp));

            // Check if this block can fit the requested size
            if (asize <= current_size) {
                // If this is our first fit or better than current best
                if (best_fit == NULL || current_size < best_size) {
                    best_fit = bp;
                    best_size = current_size;

                    // If we found an exact match, return immediately
                    if (current_size == asize) {
                        return best_fit;
                    }
                }
            }

            // Move to next block and increment depth counter
            bp = GET_NEXT(bp);
            ;
            depth++;
        }

        // If we found a fit in the current list, return it
        if (best_fit != NULL) {
            return best_fit;
        }
    }

    // No fit found
    return NULL;
}

/*
 * insert_free_block - Insert a free block into the appropriate segregated list
 */
static void insert_free_block(block *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);

    // insert at the beginning of the appropriate list
    SET_PREV(bp, NULL);
    SET_NEXT(bp, seg_lists[index]);

    if (seg_lists[index] != NULL) {
        SET_PREV(seg_lists[index], bp);
    }

    seg_lists[index] = bp;
}

/*
 * remove_free_block - Remove a free block from its segregated list
 */
static void remove_free_block(block *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);

    // adjust pointers to remove the block
    if (GET_PREV(bp) != NULL) {
        SET_NEXT(GET_PREV(bp), GET_NEXT(bp));
    } else {
        seg_lists[index] = GET_NEXT(bp);
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
        bp = seg_lists[i];
        while (bp != NULL) {
            if (GET_ALLOC(HDRP(bp))) {
                printf("Error: Block in free list is marked as allocated\n");
                correct = 0;
            }
            bp = GET_NEXT(bp);
        }
    }

    // check if there are any contiguous free blocks that escaped coalescing
    bp = heap_listp;
    while (GET_SIZE(HDRP(bp)) > 0) {
        if (!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))) {
            printf("Error: Contiguous free blocks not coalesced\n");
            correct = 0;
        }
        bp = NEXT_BLKP(bp);
    }

    // check if every free block is actually in the free list
    bp = heap_listp;
    while (GET_SIZE(HDRP(bp)) > 0) {
        if (!GET_ALLOC(HDRP(bp))) {
            int found = 0;
            int index = get_list_index(GET_SIZE(HDRP(bp)));
            void *list_bp = seg_lists[index];

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
        bp = NEXT_BLKP(bp);
    }

    // check if pointers in the free list point to valid free blocks
    for (int i = 0; i < NUM_LISTS; i++) {
        bp = seg_lists[i];
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
