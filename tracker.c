/*
 * Simple memory allocation tracker
 * Tracks allocations in 32-byte ranges up to 3.2 million bytes
 */

// Array to track allocation frequencies (index * 32 = size range)
static unsigned int alloc_tracker[100000] = {0};

// Function to record an allocation
static void record_allocation(size_t size) {
    // Calculate the bucket index (each bucket is 32 bytes wide)
    size_t index = size;
    
    // Make sure we don't go out of bounds
    if (index >= 100000) {
        // Special case for very large allocations
        alloc_tracker[99999]++;
    } else {
        alloc_tracker[index]++;
    }
}

// Structure to hold bucket info for sorting
typedef struct {
    size_t index;          // Original index
    unsigned int count;    // Allocation count
} bucket_info;

// Comparison function for qsort
static int compare_buckets(const void *a, const void *b) {
    const bucket_info *bucket_a = (const bucket_info *)a;
    const bucket_info *bucket_b = (const bucket_info *)b;
    
    // Sort by count in descending order
    if (bucket_b->count != bucket_a->count) {
        return bucket_b->count - bucket_a->count;
    }
    
    // If counts are equal, sort by size (index)
    return bucket_a->index - bucket_b->index;
}

// Function to print the top allocation frequencies
void print_top_allocations(int num_entries) {
    bucket_info buckets[100000];
    int i, count = 0;
    
    // Prepare data for sorting
    for (i = 0; i < 100000; i++) {
        if (alloc_tracker[i] > 0) {
            buckets[count].index = i;
            buckets[count].count = alloc_tracker[i];
            count++;
        }
    }
    
    // Sort buckets by frequency
    qsort(buckets, count, sizeof(bucket_info), compare_buckets);
    
    // Print header
    printf("\n===== TOP %d ALLOCATION SIZES =====\n", 
           (num_entries < count) ? num_entries : count);
    printf("%-6s %-18s %-18s %-12s\n", 
           "Rank", "Size Range (bytes)", "Occurrences", "% of Total");
    printf("-------------------------------------------------------\n");
    
    // Calculate total allocations
    unsigned int total_allocs = 0;
    for (i = 0; i < count; i++) {
        total_allocs += buckets[i].count;
    }
    
    // Print the top entries
    int print_count = (num_entries < count) ? num_entries : count;
    for (i = 0; i < print_count; i++) {
        size_t min_size = buckets[i].index;
        double percent = 100.0 * buckets[i].count / total_allocs;
        
        // Special case for the last bucket (very large allocations)
        if (buckets[i].index == 99999) {
            printf("%-6d >%-17zu %-18u %-12.2f\n", 
                   i + 1, 
                   min_size, 
                   buckets[i].count,
                   percent);
        } else {
            printf("%-6d %-6zu - %-9zu %-18u %-12.2f\n", 
                   i + 1, 
                   min_size, 
                   max_size, 
                   buckets[i].count,
                   percent);
        }
    }
    
    // Print summary
    printf("\nTotal allocations: %u\n", total_allocs);
    printf("Total buckets with data: %d\n", count);
}

// Reset all statistics
void reset_allocation_stats() {
    memset(alloc_tracker, 0, sizeof(alloc_tracker));
}

