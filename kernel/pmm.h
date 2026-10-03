#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>
#include <stddef.h>
#include "include/limine.h"

#define PAGE_SIZE 4096ULL

extern uint64_t g_hhdm_offset;

#define PHYS_TO_VIRT(p) ((void *)((uint64_t)(p) + g_hhdm_offset))
#define VIRT_TO_PHYS(v) ((void *)((uint64_t)(v) - g_hhdm_offset))

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm);
void *pmm_alloc_page(void);
void pmm_free_page(void *phys_addr);
void *pmm_alloc_pages(size_t count);
void pmm_free_pages(void *phys_addr, size_t count);

uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_used_memory(void);

#endif
