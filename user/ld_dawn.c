/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"
#include "musl_compat.h"

/*
 * DAWN Dynamic Linker (ld-dawn.so)
 * Handles ELF64 Dynamic linking, symbol resolution and relocation.
 */

#define PT_DYNAMIC 2
#define DT_NULL    0
#define DT_NEEDED  1
#define DT_PLTRELSZ 2
#define DT_PLTGOT  3
#define DT_HASH    4
#define DT_STRTAB  5
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_STRSZ   10
#define DT_SYMENT  11
#define DT_INIT    12
#define DT_FINI    13
#define DT_SONAME  14
#define DT_RPATH   15
#define DT_SYMBOLIC 16
#define DT_REL     17
#define DT_JMPREL  23

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_GOT32     3
#define R_X86_64_PLT32     4
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

struct elf64_dyn {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
};

struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

struct elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

#define ELF64_R_SYM(i)   ((i) >> 32)
#define ELF64_R_TYPE(i)  ((i) & 0xffffffffL)

int ld_dawn_relocate(uintptr_t base_addr, const struct elf64_dyn *dynamic) {
    if (dynamic == NULL) return -1;

    const struct elf64_rela *rela = NULL;
    size_t relasz = 0;
    size_t relaent = sizeof(struct elf64_rela);
    const struct elf64_sym *symtab = NULL;

    for (const struct elf64_dyn *d = dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_RELA:
                rela = (const struct elf64_rela *)(base_addr + d->d_un.d_ptr);
                break;
            case DT_RELASZ:
                relasz = d->d_un.d_val;
                break;
            case DT_RELAENT:
                relaent = d->d_un.d_val;
                break;
            case DT_SYMTAB:
                symtab = (const struct elf64_sym *)(base_addr + d->d_un.d_ptr);
                break;
            default:
                break;
        }
    }

    if (rela != NULL && relasz > 0) {
        size_t count = relasz / relaent;
        for (size_t i = 0; i < count; i++) {
            uint32_t type = (uint32_t)ELF64_R_TYPE(rela[i].r_info);
            uint32_t sym_idx = (uint32_t)ELF64_R_SYM(rela[i].r_info);
            uint64_t *target = (uint64_t *)(base_addr + rela[i].r_offset);

            if (type == R_X86_64_RELATIVE) {
                *target = base_addr + (uint64_t)rela[i].r_addend;
            } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                if (symtab != NULL) {
                    *target = base_addr + symtab[sym_idx].st_value;
                }
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    dawn_println("DAWN-OS Dynamic Linker (ld-dawn.so v1.0) initialized.");
    return 0;
}
