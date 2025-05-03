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
    ""
};

/* Basic constants and macros */
#define WSIZE 4             // word size (bytes)
#define DSIZE 8             // double word size (bytes)
#define CHUNKSIZE (1<<12)   // initial heap size (bytes)
#define MINIMUM 16          // minimum block size (bytes)

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
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// given block ptr bp, compute address of next and previous blocks
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// get and set predecessor and successor pointers for free blocks
#define PRED_PTR(bp) ((char *)(bp))
#define SUCC_PTR(bp) ((char *)(bp) + WSIZE)
#define GET_PRED(bp) (*(char **)(bp))
#define GET_SUCC(bp) (*(char **)(SUCC_PTR(bp)))
#define SET_PRED(bp, ptr) (*(char **)(bp) = (ptr))
#define SET_SUCC(bp, ptr) (*(char **)(SUCC_PTR(bp)) = (ptr))

// number of segregated free lists and their size thresholds
#define NUM_LISTS 10
static int get_list_index(size_t size) {
    if (size <= 32)
        return 0;
    else if (size <= 64)
        return 1;
    else if (size <= 128)
        return 2;
    else if (size <= 256)
        return 3;
    else if (size <= 512)
        return 4;
    else if (size <= 1024)
        return 5;
    else if (size <= 2048)
        return 6;
    else if (size <= 4096)
        return 7;
    else if (size <= 8192)
        return 8;
    else
        return 9;
}

// global variables
static char *heap_listp = 0;  // pointer to first block
static char *seg_lists[NUM_LISTS]; // array of segregated free list heads

// function prototypes for internal helper routines
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void insert_free_block(void *bp);
static void remove_free_block(void *bp);
static int get_list_index(size_t size);

/*
 * mm_init - Initialize the memory manager
 */
int mm_init(void) {
    // initialize all segregated free lists to NULL
    for (int i = 0; i < NUM_LISTS; i++) {
        seg_lists[i] = NULL;
    }
    
    // create the initial empty heap
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    
    PUT(heap_listp, 0);                            // alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));   // prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));   // prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));       // epilogue header
    heap_listp += (2*WSIZE);                       // point to prologue footer
    
    // extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    
    return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload
 */
void *mm_malloc(size_t size) {
    size_t asize;      // adjusted block size
    size_t extendsize; // amount to extend heap if no fit
    char *bp;
    
    // ignore spurious requests
    if (size == 0)
        return NULL;
    
    // adjust block size to include overhead and alignment requirements
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    // search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    
    // no fit found. get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
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
    void *oldptr = ptr;
    void *newptr;
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
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    // if the new size is smaller than the old size, we can just shrink
    if (asize <= oldsize) {
        // only split if the remaining chunk would be large enough
        if (oldsize - asize >= 2*DSIZE) {
            PUT(HDRP(oldptr), PACK(asize, 1));
            PUT(FTRP(oldptr), PACK(asize, 1));
            void *next_bp = NEXT_BLKP(oldptr);
            PUT(HDRP(next_bp), PACK(oldsize - asize, 0));
            PUT(FTRP(next_bp), PACK(oldsize - asize, 0));
            insert_free_block(next_bp);
        }
        return oldptr;
    }
    
    // check if the next block is free and can be used for expansion
    void *next = NEXT_BLKP(oldptr);
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
    copySize = oldsize - DSIZE;  // exclude header and footer
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
    char *bp;
    size_t size;
    
    // allocate an even number of words to maintain alignment
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if (size < MINIMUM) size = MINIMUM;
    
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
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    
    // remove the block from the free list
    remove_free_block(bp);
    
    // if the remaining part is large enough for a new free block
    if ((csize - asize) >= (2*DSIZE)) {
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
    void *bp;
    
    // search from the appropriate list up through larger lists
    for (int i = list_index; i < NUM_LISTS; i++) {
        bp = seg_lists[i];
        // first-fit search through the list
        while (bp != NULL) {
            if (asize <= GET_SIZE(HDRP(bp))) {
                return bp;
            }
            bp = GET_SUCC(bp);
        }
    }
    
    // no fit found
    return NULL;
}

/*
 * insert_free_block - Insert a free block into the appropriate segregated list
 */
static void insert_free_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);
    
    // insert at the beginning of the appropriate list
    SET_PRED(bp, NULL);
    SET_SUCC(bp, seg_lists[index]);
    
    if (seg_lists[index] != NULL) {
        SET_PRED(seg_lists[index], bp);
    }
    
    seg_lists[index] = bp;
}

/*
 * remove_free_block - Remove a free block from its segregated list
 */
static void remove_free_block(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int index = get_list_index(size);
    
    // adjust pointers to remove the block
    if (GET_PRED(bp) != NULL) {
        SET_SUCC(GET_PRED(bp), GET_SUCC(bp));
    } else {
        seg_lists[index] = GET_SUCC(bp);
    }
    
    if (GET_SUCC(bp) != NULL) {
        SET_PRED(GET_SUCC(bp), GET_PRED(bp));
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
            bp = GET_SUCC(bp);
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
                list_bp = GET_SUCC(list_bp);
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
            if (GET_SUCC(bp) != NULL && ((void*)GET_SUCC(bp) < mem_heap_lo() || (void*)GET_SUCC(bp) > mem_heap_hi())) {
                printf("Error: Successor pointer in free block points outside heap\n");
                correct = 0;
            }
            if (GET_PRED(bp) != NULL && ((void*)GET_PRED(bp) < mem_heap_lo() || (void*)GET_PRED(bp) > mem_heap_hi())) {
                printf("Error: Predecessor pointer in free block points outside heap\n");
                correct = 0;
            }
            bp = GET_SUCC(bp);
        }
    }
    
    return correct;
}
