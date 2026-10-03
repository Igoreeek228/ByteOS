#ifndef KERNEL_KHEAP_H
#define KERNEL_KHEAP_H

#include <stdint.h>
#include <stddef.h>

void kheap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t new_size);

uint64_t kheap_get_used(void);
uint64_t kheap_get_free(void);

#endif
