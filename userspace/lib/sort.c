#include "stdlib.h"
#include "string.h"

/*
 * Introsort: quicksort with a median-of-three pivot that always recurses into
 * the smaller partition (so the stack stays O(log n)), heapsort once the
 * recursion budget of 2*log2(n) is spent, and insertion sort for short runs.
 */

#define SORT_INSERTION_LIMIT 16
#define SWAP_CHUNK           32

static void swap_elements(char *a, char *b, size_t size) {
    unsigned char scratch[SWAP_CHUNK];
    if (a == b)
        return;
    while (size > 0) {
        size_t n = size < SWAP_CHUNK ? size : SWAP_CHUNK;
        memcpy(scratch, a, n);
        memcpy(a, b, n);
        memcpy(b, scratch, n);
        a += n;
        b += n;
        size -= n;
    }
}

static void insertion_sort(char *base, size_t count, size_t size, CompareFunction compare) {
    for (size_t i = 1; i < count; ++i) {
        for (size_t j = i; j > 0 && compare(base + (j - 1) * size, base + j * size) > 0; --j)
            swap_elements(base + (j - 1) * size, base + j * size, size);
    }
}

static void sift_down(char *base, size_t root, size_t count, size_t size,
                      CompareFunction compare) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count)
            return;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0)
            ++child;
        if (compare(base + root * size, base + child * size) >= 0)
            return;
        swap_elements(base + root * size, base + child * size, size);
        root = child;
    }
}

static void heap_sort(char *base, size_t count, size_t size, CompareFunction compare) {
    for (size_t i = count / 2; i-- > 0;)
        sift_down(base, i, count, size, compare);
    for (size_t end = count - 1; end > 0; --end) {
        swap_elements(base, base + end * size, size);
        sift_down(base, 0, end, size, compare);
    }
}

/* Puts the median of first, middle and last element at index 0; the other two
   end up as sentinels (<= pivot in the middle, >= pivot at the end). */
static void median_to_front(char *base, size_t count, size_t size, CompareFunction compare) {
    char *first = base;
    char *middle = base + (count / 2) * size;
    char *last = base + (count - 1) * size;
    if (compare(middle, first) < 0)
        swap_elements(middle, first, size);
    if (compare(last, middle) < 0) {
        swap_elements(last, middle, size);
        if (compare(middle, first) < 0)
            swap_elements(middle, first, size);
    }
    swap_elements(first, middle, size);
}

/* Moves the pivot at index 0 to its final index, which is returned. Elements
   before it are <= the pivot, elements after it are >= the pivot. */
static size_t partition(char *base, size_t count, size_t size, CompareFunction compare) {
    size_t i = 0;
    size_t j = count;
    for (;;) {
        do {
            ++i;
        } while (i < count && compare(base + i * size, base) < 0);
        do {
            --j;
        } while (j > 0 && compare(base + j * size, base) > 0);
        if (i >= j)
            break;
        swap_elements(base + i * size, base + j * size, size);
    }
    swap_elements(base, base + j * size, size);
    return j;
}

static void introsort(char *base, size_t count, size_t size, CompareFunction compare,
                      unsigned budget) {
    while (count > SORT_INSERTION_LIMIT) {
        if (budget == 0) {
            heap_sort(base, count, size, compare);
            return;
        }
        --budget;
        median_to_front(base, count, size, compare);
        size_t pivot = partition(base, count, size, compare);
        size_t right_count = count - pivot - 1;
        if (pivot < right_count) {
            introsort(base, pivot, size, compare, budget);
            base += (pivot + 1) * size;
            count = right_count;
        } else {
            introsort(base + (pivot + 1) * size, right_count, size, compare, budget);
            count = pivot;
        }
    }
    insertion_sort(base, count, size, compare);
}

void qsort(void *base, size_t count, size_t size, CompareFunction compare) {
    unsigned budget = 0;
    if (!base || count < 2 || size == 0)
        return;
    for (size_t n = count; n > 1; n >>= 1)
        budget += 2;
    introsort((char *)base, count, size, compare, budget);
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              CompareFunction compare) {
    const char *items = (const char *)base;
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = compare(key, items + middle * size);
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1;
        else
            return (void *)(items + middle * size);
    }
    return 0;
}
