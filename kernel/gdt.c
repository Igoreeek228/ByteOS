#include "gdt.h"

struct gdt_descriptor {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

static struct {
    struct gdt_entry entries[5];
    struct tss_descriptor tss;
} __attribute__((packed)) gdt_table;

static struct gdt_descriptor gdt_ptr;
static struct tss_entry kernel_tss;

/* 8KB dedicated stack for Double Fault / IST1 */
static uint8_t double_fault_stack[8192] __attribute__((aligned(16)));

static void set_gdt_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_table.entries[num].base_low = (base & 0xFFFF);
    gdt_table.entries[num].base_mid = ((base >> 16) & 0xFF);
    gdt_table.entries[num].base_high = ((base >> 24) & 0xFF);

    gdt_table.entries[num].limit_low = (limit & 0xFFFF);
    gdt_table.entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_table.entries[num].access = access;
}

static void set_tss_gate(uint64_t base, uint32_t limit) {
    gdt_table.tss.length = limit & 0xFFFF;
    gdt_table.tss.base_low = base & 0xFFFF;
    gdt_table.tss.base_mid = (base >> 16) & 0xFF;
    gdt_table.tss.flags1 = 0x89; /* Present, 64-bit TSS (Available) */
    gdt_table.tss.flags2 = 0x00;
    gdt_table.tss.base_high = (base >> 24) & 0xFF;
    gdt_table.tss.base_upper32 = (base >> 32) & 0xFFFFFFFF;
    gdt_table.tss.reserved = 0;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_table) - 1;
    gdt_ptr.base = (uint64_t)&gdt_table;

    /* 0: Null descriptor */
    set_gdt_gate(0, 0, 0, 0, 0);

    /* 1: 0x08 - Kernel Code 64-bit */
    set_gdt_gate(1, 0, 0xFFFFF, 0x9A, 0xAF);

    /* 2: 0x10 - Kernel Data 64-bit */
    set_gdt_gate(2, 0, 0xFFFFF, 0x92, 0xCF);

    /* 3: 0x18 - User Data 64-bit */
    set_gdt_gate(3, 0, 0xFFFFF, 0xF2, 0xCF);

    /* 4: 0x20 - User Code 64-bit */
    set_gdt_gate(4, 0, 0xFFFFF, 0xFA, 0xAF);

    /* Setup TSS */
    for (size_t i = 0; i < sizeof(kernel_tss); i++) {
        ((uint8_t *)&kernel_tss)[i] = 0;
    }
    kernel_tss.ist1 = (uint64_t)double_fault_stack + sizeof(double_fault_stack);
    kernel_tss.iopb_offset = sizeof(kernel_tss);

    set_tss_gate((uint64_t)&kernel_tss, sizeof(kernel_tss) - 1);

    /* Load GDT */
    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));

    /* Reload data segment registers */
    __asm__ volatile (
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        : : : "ax"
    );

    /* Far jump / return to reload CS to 0x08 */
    __asm__ volatile (
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : : "rax", "memory"
    );

    /* Load TSS into TR register */
    __asm__ volatile (
        "mov $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        : : : "ax"
    );
}
