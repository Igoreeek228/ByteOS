#include "kheap.h"
#include "pmm.h"

typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define ALIGN16(x) (((x) + 15) & ~((size_t)15))

static block_header_t *heap_start = 0;
static uint64_t heap_used_bytes = 0;
static uint64_t heap_total_bytes = 0;

/* Expand heap by allocating n 4KB pages from PMM */
static block_header_t *kheap_expand(size_t min_bytes) {
    size_t needed = ALIGN16(min_bytes + HEADER_SIZE);
    size_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < 4) pages = 4; /* Allocate at least 16KB at a time */

    void *phys = pmm_alloc_pages(pages);
    if (!phys) return 0;

    block_header_t *block = (block_header_t *)PHYS_TO_VIRT(phys);
    block->size = (pages * PAGE_SIZE) - HEADER_SIZE;
    block->is_free = 1;
    block->next = 0;
    block->prev = 0;

    heap_total_bytes += (pages * PAGE_SIZE);

    if (!heap_start) {
        heap_start = block;
        return block;
    }

    block_header_t *curr = heap_start;
    while (curr->next) {
        curr = curr->next;
    }
    curr->next = block;
    block->prev = curr;
    return block;
}

void kheap_init(void) {
    heap_start = 0;
    heap_used_bytes = 0;
    heap_total_bytes = 0;
    kheap_expand(64 * 1024); /* Start with 64KB */
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;
    size = ALIGN16(size);

    block_header_t *curr = heap_start;
    block_header_t *best = 0;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            best = curr;
            break;
        }
        curr = curr->next;
    }

    if (!best) {
        best = kheap_expand(size);
        if (!best) return 0;
    }

    /* Split block if remainder is large enough */
    if (best->size >= size + HEADER_SIZE + 32) {
        block_header_t *split = (block_header_t *)((uint8_t *)best + HEADER_SIZE + size);
        split->size = best->size - size - HEADER_SIZE;
        split->is_free = 1;
        split->next = best->next;
        split->prev = best;

        if (best->next) {
            best->next->prev = split;
        }
        best->next = split;
        best->size = size;
    }

    best->is_free = 0;
    heap_used_bytes += (best->size + HEADER_SIZE);

    return (void *)((uint8_t *)best + HEADER_SIZE);
}

void kfree(void *ptr) {
    if (!ptr) return;

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (block->is_free) return;

    block->is_free = 1;
    if (heap_used_bytes >= (block->size + HEADER_SIZE)) {
        heap_used_bytes -= (block->size + HEADER_SIZE);
    }

    /* Coalesce with next block if free */
    if (block->next && block->next->is_free) {
        /* Check if they are physically contiguous in virtual memory */
        if ((uint8_t *)block + HEADER_SIZE + block->size == (uint8_t *)block->next) {
            block->size += HEADER_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                block->next->prev = block;
            }
        }
    }

    /* Coalesce with prev block if free */
    if (block->prev && block->prev->is_free) {
        if ((uint8_t *)block->prev + HEADER_SIZE + block->prev->size == (uint8_t *)block) {
            block->prev->size += HEADER_SIZE + block->size;
            block->prev->next = block->next;
            if (block->next) {
                block->next->prev = block->prev;
            }
        }
    }
}

void *kcalloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < total; i++) p[i] = 0;
    }
    return ptr;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    new_size = ALIGN16(new_size);

    if (block->size >= new_size) {
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < block->size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}

uint64_t kheap_get_used(void) {
    return heap_used_bytes;
}

uint64_t kheap_get_free(void) {
    if (heap_total_bytes > heap_used_bytes) {
        return heap_total_bytes - heap_used_bytes;
    }
    return 0;
}
