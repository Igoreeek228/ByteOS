#include "pmm.h"

uint64_t g_hhdm_offset = 0;

static uint8_t *bitmap = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t total_memory_bytes = 0;
static uint64_t last_scanned_idx = 0;

static inline void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1 << (page % 8));
}

static inline void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1 << (page % 8));
}

static inline int bitmap_test(uint64_t page) {
    return (bitmap[page / 8] & (1 << (page % 8))) != 0;
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm) {
    g_hhdm_offset = hhdm;

    if (!memmap || memmap->entry_count == 0) return;

    uint64_t highest_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total_memory_bytes += entry->length;
        }
        uint64_t top = entry->base + entry->length;
        if (top > highest_addr) {
            highest_addr = top;
        }
    }

    total_pages = highest_addr / PAGE_SIZE;
    uint64_t bitmap_size = (total_pages + 7) / 8;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Find a usable memory region big enough to hold the bitmap */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap_phys = entry->base;
            break;
        }
    }

    if (bitmap_phys == 0) return;

    bitmap = (uint8_t *)PHYS_TO_VIRT(bitmap_phys);

    /* Initially mark all pages as used */
    for (uint64_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }

    free_pages = 0;

    /* Mark usable areas as free */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_page = entry->base / PAGE_SIZE;
            uint64_t pages_count = entry->length / PAGE_SIZE;
            for (uint64_t p = 0; p < pages_count; p++) {
                bitmap_clear(start_page + p);
                free_pages++;
            }
        }
    }

    /* Reserve page 0 and bitmap pages */
    bitmap_set(0);
    if (free_pages > 0) free_pages--;

    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;
    for (uint64_t p = 0; p < bitmap_pages; p++) {
        if (!bitmap_test(bitmap_start_page + p)) {
            bitmap_set(bitmap_start_page + p);
            if (free_pages > 0) free_pages--;
        }
    }
}

void *pmm_alloc_page(void) {
    if (free_pages == 0 || total_pages == 0) return 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t idx = (last_scanned_idx + i) % total_pages;
        if (!bitmap_test(idx)) {
            bitmap_set(idx);
            free_pages--;
            last_scanned_idx = idx + 1;
            return (void *)(idx * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *phys_addr) {
    if (!phys_addr) return;
    uint64_t page = (uint64_t)phys_addr / PAGE_SIZE;
    if (page >= total_pages) return;

    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0 || free_pages < count) return 0;
    if (count == 1) return pmm_alloc_page();

    uint64_t consecutive = 0;
    uint64_t start_page = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) start_page = i;
            consecutive++;
            if (consecutive == count) {
                for (uint64_t p = 0; p < count; p++) {
                    bitmap_set(start_page + p);
                }
                free_pages -= count;
                return (void *)(start_page * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }
    return 0;
}

void pmm_free_pages(void *phys_addr, size_t count) {
    if (!phys_addr || count == 0) return;
    uint64_t start_page = (uint64_t)phys_addr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        if (start_page + i < total_pages && bitmap_test(start_page + i)) {
            bitmap_clear(start_page + i);
            free_pages++;
        }
    }
}

uint64_t pmm_get_total_memory(void) {
    return total_memory_bytes;
}

uint64_t pmm_get_free_memory(void) {
    return free_pages * PAGE_SIZE;
}

uint64_t pmm_get_used_memory(void) {
    if (total_memory_bytes > (free_pages * PAGE_SIZE)) {
        return total_memory_bytes - (free_pages * PAGE_SIZE);
    }
    return 0;
}
