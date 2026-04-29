/* multiboot kernel main.c
 * 演示 Multiboot 规范中 bootloader 传给内核的 boot_info 结构
 *
 * 编译:
 *   as --32 multiboot.S -o multiboot.o
 *   gcc -m32 -fno-pie -c main.c -o main.o
 *   ld -m elf_i386 -Ttext 0x100000 -o kernel.elf multiboot.o main.o
 *
 * 用 QEMU 测试:
 *   qemu-system-i386 -kernel kernel.elf
 *   (或 qemu-system-i386 -cdrom kernel.iso 如果打包成 ISO)
 */

typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

/* Multiboot magic - GRUB 放在 eax 里 */
#define MULTIBOOT_MAGIC      0x2BADB002

/* boot_info flags (从 ebx 指向的结构里读) */
#define MB_FLAG_MEM          (1 << 0)
#define MB_FLAG_BOOTDEV      (1 << 1)
#define MB_FLAG_CMDLINE      (1 << 2)
#define MB_FLAG_MODS         (1 << 3)
#define MB_FLAG_AOUT         (1 << 4)
#define MB_FLAG_ELF          (1 << 5)
#define MB_FLAG_MMAP         (1 << 6)
#define MB_FLAG_DRIVES       (1 << 7)
#define MB_FLAG_CONFIG       (1 << 8)
#define MB_FLAG_LOADER       (1 << 9)
#define MB_FLAG_APM          (1 << 10)
#define MB_FLAG_VBE          (1 << 11)

/* Memory map entry types */
#define MB_MEM_AVAILABLE     1
#define MB_MEM_RESERVED      2
#define MB_MEM_ACPI_RECLAIM  3
#define MB_MEM_NVS          4

#pragma pack(push, 1)
/* boot_info 结构 (GRUB 放在 ebx 指向的内存) */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;         /* KB */
    uint32_t mem_upper;         /* KB */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    union {
        struct { uint32_t tabsize, strsize, addr, pad; } aout;
        struct { uint32_t num, size, addr, shndx; } elf;
    } syms;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
};
typedef struct multiboot_info multiboot_info_t;

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
};
typedef struct multiboot_mmap_entry multiboot_mmap_entry_t;
#pragma pack(pop)

/* 写 VGA 文本内存 */
static inline void vga_putchar(int *x, int *y, char c, uint8_t attr) {
    volatile uint16_t *vga = (uint16_t *)0xB8000;
    int cols = 80;
    if (c == '\n') { *y = (*y + 1); *x = 0; }
    else {
        vga[*y * cols + *x] = (attr << 8) | c;
        *x = (*x + 1);
        if (*x >= cols) { *x = 0; *y = (*y + 1); }
    }
    if (*y >= 25) {
        volatile uint16_t *src = (uint16_t *)0xB8000 + cols;
        volatile uint16_t *dst = (uint16_t *)0xB8000;
        for (int i = 0; i < 24 * cols; i++) dst[i] = src[i];
        for (int i = 0; i < cols; i++) dst[24 * cols + i] = (7 << 8) | ' ';
        *y = 24;
    }
}

void print_str(int *x, int *y, const char *s, uint8_t attr) {
    while (*s) vga_putchar(x, y, *s++, attr);
}

void print_hex(int *x, int *y, uint32_t val, uint8_t attr) {
    char hex[] = "0123456789ABCDEF";
    char buf[11] = "0x";
    for (int i = 0; i < 8; i++) buf[9 - i] = hex[(val >> (4 * i)) & 0xF];
    print_str(x, y, buf, attr);
}

void print_int(int *x, int *y, uint32_t val, uint8_t attr) {
    char buf[16];
    int i = 0;
    if (val == 0) { vga_putchar(x, y, '0', attr); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i--) vga_putchar(x, y, buf[i], attr);
}

/* 内核主入口 */
void kernel_main(unsigned long magic, multiboot_info_t *info) {
    int x = 0, y = 0;
    uint8_t title = 0x0F, normal = 0x07, green = 0x0A, yellow = 0x0E;

    /* 清屏 */
    volatile uint16_t *vga = (uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = (7 << 8) | ' ';

    print_str(&x, &y, "=== Multiboot Kernel Demo ===", title); y++; x = 0;

    print_str(&x, &y, "Magic: ", normal);
    print_hex(&x, &y, magic, green);
    print_str(&x, &y, magic == MULTIBOOT_MAGIC ? " (OK)" : " (BAD!)",
              magic == MULTIBOOT_MAGIC ? green : 0x0C);
    y++; x = 0;

    if (info->flags & MB_FLAG_LOADER) {
        print_str(&x, &y, "Loader: ", normal);
        print_str(&x, &y, (char *)(uintptr_t)info->boot_loader_name, green);
        y++; x = 0;
    }

    if (info->flags & MB_FLAG_CMDLINE) {
        print_str(&x, &y, "Cmdline: ", normal);
        print_str(&x, &y, (char *)(uintptr_t)info->cmdline, yellow);
        y++; x = 0;
    }

    if (info->flags & MB_FLAG_MEM) {
        print_str(&x, &y, "Low mem: ", normal);
        print_int(&x, &y, info->mem_lower, green);
        print_str(&x, &y, " KB  High: ", normal);
        print_int(&x, &y, info->mem_upper, green);
        print_str(&x, &y, " KB", normal);
        y++; x = 0;
    }

    if (info->flags & MB_FLAG_MMAP) {
        print_str(&x, &y, "Memory map:", normal); y++; x = 0;
        multiboot_mmap_entry_t *mmap = (multiboot_mmap_entry_t *)(uintptr_t)info->mmap_addr;
        int count = 0;
        while ((uint32_t)mmap < info->mmap_addr + info->mmap_length && count < 4) {
            if (mmap->type == MB_MEM_AVAILABLE) {
                print_str(&x, &y, "  [FREE] ", green);
                print_hex(&x, &y, (uint32_t)mmap->addr, yellow);
                print_str(&x, &y, " - ", normal);
                print_hex(&x, &y, (uint32_t)(mmap->addr + mmap->len), yellow);
                print_str(&x, &y, " (", normal);
                print_int(&x, &y, (uint32_t)(mmap->len / 1024 / 1024), green);
                print_str(&x, &y, " MB)", normal);
                y++; x = 0;
                count++;
            }
            mmap = (multiboot_mmap_entry_t *)((uint8_t *)mmap + mmap->size + sizeof(uint32_t));
        }
    }

    if (info->flags & MB_FLAG_MODS) {
        print_str(&x, &y, "Modules: ", normal);
        print_int(&x, &y, info->mods_count, green);
        y++; x = 0;
    }

    print_str(&x, &y, "Halted.", normal);
    while (1) __asm__ volatile ("hlt");
}
