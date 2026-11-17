/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "elf_loader.h"

#define ELF64_USER_LIMIT (1ULL << 47)
#define ELF64_PAGE_SIZE 4096ULL
#define ELF_IDENT_CLASS_64 2U
#define ELF_IDENT_DATA_LSB 1U
#define ELF_IDENT_VERSION 1U
#define ELF_TYPE_EXEC 2U
#define ELF_MACHINE_X86_64 62U
#define ELF_VERSION_CURRENT 1U
#define ELF_PROGRAM_LOAD 1U
#define ELF_PROGRAM_DYNAMIC 2U
#define ELF_PROGRAM_INTERPRETER 3U
#define ELF_PROGRAM_NOTE 4U
#define ELF_PROGRAM_PHDR 6U
#define ELF_PROGRAM_GNU_STACK 0x6474e551U
#define ELF_PROGRAM_GNU_RELRO 0x6474e552U
#define ELF_FLAG_EXECUTE 1U
#define ELF_FLAG_WRITE 2U
#define ELF_FLAG_READ 4U
#define ELF_FLAG_MASK 7U

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

_Static_assert(sizeof(struct elf64_header)==64U,
               "ELF64 header layout mismatch");
_Static_assert(sizeof(struct elf64_program_header)==56U,
               "ELF64 program header layout mismatch");

static void copy_bytes(void *destination,const void *source,size_t count) {
    uint8_t *output=(uint8_t *)destination;
    const uint8_t *input=(const uint8_t *)source;
    for (size_t index=0U;index<count;index++) output[index]=input[index];
}

static bool range_inside_image(uint64_t offset,uint64_t length,
                               size_t image_size) {
    const uint64_t total=(uint64_t)image_size;
    return offset<=total && length<=total-offset;
}

static bool power_of_two(uint64_t value) {
    return value!=0ULL && (value&(value-1ULL))==0ULL;
}

bool elf64_validate_image(const void *image,size_t image_size,
                          struct elf64_image_info *info) {
    const uint8_t *bytes=(const uint8_t *)image;
    struct elf64_header header={0};
    uint64_t table_size=0ULL;
    uint64_t entry_executable=0ULL;
    uint16_t load_count=0U;
    if (image==NULL || info==NULL || image_size<sizeof(header))
        return false;
    copy_bytes(&header,bytes,sizeof(header));
    if (header.ident[0]!=0x7fU || header.ident[1]!=(uint8_t)'E' ||
        header.ident[2]!=(uint8_t)'L' || header.ident[3]!=(uint8_t)'F' ||
        header.ident[4]!=ELF_IDENT_CLASS_64 ||
        header.ident[5]!=ELF_IDENT_DATA_LSB ||
        header.ident[6]!=ELF_IDENT_VERSION ||
        header.type!=ELF_TYPE_EXEC ||
        header.machine!=ELF_MACHINE_X86_64 ||
        header.version!=ELF_VERSION_CURRENT ||
        header.header_size!=sizeof(struct elf64_header) ||
        header.program_header_entry_size!=sizeof(struct elf64_program_header) ||
        header.program_header_count==0U ||
        header.program_header_count>DAWN_ELF64_MAX_PROGRAM_HEADERS ||
        header.program_header_offset<sizeof(struct elf64_header))
        return false;

    table_size=(uint64_t)header.program_header_count*
               (uint64_t)sizeof(struct elf64_program_header);
    if (!range_inside_image(header.program_header_offset,table_size,image_size))
        return false;

    for (uint16_t index=0U;index<header.program_header_count;index++) {
        struct elf64_program_header current={0};
        const uint64_t offset=header.program_header_offset+
            (uint64_t)index*(uint64_t)sizeof(current);
        uint64_t memory_end=0ULL,page_end=0ULL,page_start=0ULL;
        if (!range_inside_image(offset,sizeof(current),image_size))
            return false;
        copy_bytes(&current,bytes+(size_t)offset,sizeof(current));
        if (current.type!=ELF_PROGRAM_LOAD) {
            if (current.type==ELF_PROGRAM_DYNAMIC ||
                current.type==ELF_PROGRAM_INTERPRETER)
                return false;
            if (current.type!=0U && current.type!=ELF_PROGRAM_NOTE &&
                current.type!=ELF_PROGRAM_PHDR &&
                current.type!=ELF_PROGRAM_GNU_STACK &&
                current.type!=ELF_PROGRAM_GNU_RELRO)
                return false;
            continue;
        }

        if (current.memory_size==0ULL ||
            current.file_size>current.memory_size ||
            (current.flags&~(uint32_t)ELF_FLAG_MASK)!=0U ||
            (current.flags&(ELF_FLAG_WRITE|ELF_FLAG_EXECUTE))==
                (ELF_FLAG_WRITE|ELF_FLAG_EXECUTE) ||
            !range_inside_image(current.offset,current.file_size,image_size) ||
            current.virtual_address<0x10000ULL ||
            current.virtual_address>=ELF64_USER_LIMIT ||
            current.memory_size>UINT64_MAX-current.virtual_address)
            return false;
        memory_end=current.virtual_address+current.memory_size;
        if (memory_end>ELF64_USER_LIMIT ||
            (current.virtual_address&(ELF64_PAGE_SIZE-1ULL))!=
                (current.offset&(ELF64_PAGE_SIZE-1ULL)))
            return false;
        if (current.alignment>1ULL &&
            (!power_of_two(current.alignment) ||
             current.virtual_address%current.alignment!=
                 current.offset%current.alignment))
            return false;
        page_start=current.virtual_address&~(ELF64_PAGE_SIZE-1ULL);
        if (memory_end>UINT64_MAX-(ELF64_PAGE_SIZE-1ULL))
            return false;
        page_end=(memory_end+ELF64_PAGE_SIZE-1ULL)&
                 ~(ELF64_PAGE_SIZE-1ULL);
        if (page_end>ELF64_USER_LIMIT) return false;

        /* Reject page-level overlap: this initial loader subset cannot merge
         * segments with different permissions inside one virtual page. */
        for (uint16_t previous_index=0U;previous_index<index;previous_index++) {
            struct elf64_program_header previous={0};
            const uint64_t previous_offset=header.program_header_offset+
                (uint64_t)previous_index*(uint64_t)sizeof(previous);
            uint64_t previous_end=0ULL,previous_page_end=0ULL;
            uint64_t previous_page_start=0ULL;
            copy_bytes(&previous,bytes+(size_t)previous_offset,sizeof(previous));
            if (previous.type!=ELF_PROGRAM_LOAD || previous.memory_size==0ULL)
                continue;
            previous_end=previous.virtual_address+previous.memory_size;
            previous_page_start=previous.virtual_address&
                                ~(ELF64_PAGE_SIZE-1ULL);
            previous_page_end=(previous_end+ELF64_PAGE_SIZE-1ULL)&
                              ~(ELF64_PAGE_SIZE-1ULL);
            if (page_start<previous_page_end &&
                previous_page_start<page_end)
                return false;
        }
        if ((current.flags&ELF_FLAG_EXECUTE)!=0U &&
            header.entry>=current.virtual_address &&
            header.entry<current.virtual_address+current.file_size)
            entry_executable=1ULL;
        ++load_count;
    }
    if (load_count==0U || entry_executable==0ULL) return false;
    info->entry=header.entry;
    info->load_segment_count=load_count;
    return true;
}

#ifdef DAWN_KERNEL_TEST
static void test_store_bytes(void *destination,const void *source,
                             size_t count) {
    copy_bytes(destination,source,count);
}

static void test_make_image(uint8_t *image,size_t image_size,
                            struct elf64_header *header,
                            struct elf64_program_header *program) {
    for (size_t index=0U;index<image_size;index++) image[index]=0U;
    *header=(struct elf64_header){0};
    header->ident[0]=0x7fU;
    header->ident[1]=(uint8_t)'E';
    header->ident[2]=(uint8_t)'L';
    header->ident[3]=(uint8_t)'F';
    header->ident[4]=ELF_IDENT_CLASS_64;
    header->ident[5]=ELF_IDENT_DATA_LSB;
    header->ident[6]=ELF_IDENT_VERSION;
    header->type=ELF_TYPE_EXEC;
    header->machine=ELF_MACHINE_X86_64;
    header->version=ELF_VERSION_CURRENT;
    header->entry=0x400000ULL;
    header->program_header_offset=sizeof(struct elf64_header);
    header->header_size=(uint16_t)sizeof(struct elf64_header);
    header->program_header_entry_size=
        (uint16_t)sizeof(struct elf64_program_header);
    header->program_header_count=1U;
    *program=(struct elf64_program_header){
        .type=ELF_PROGRAM_LOAD,
        .flags=ELF_FLAG_READ|ELF_FLAG_EXECUTE,
        .offset=0x1000ULL,
        .virtual_address=0x400000ULL,
        .file_size=16ULL,
        .memory_size=32ULL,
        .alignment=ELF64_PAGE_SIZE
    };
    test_store_bytes(image,header,sizeof(*header));
    test_store_bytes(image+(size_t)header->program_header_offset,
                     program,sizeof(*program));
}

bool elf64_loader_self_test(void) {
    uint8_t image[8192]={0U};
    struct elf64_header header={0};
    struct elf64_program_header program={0};
    struct elf64_program_header second={0};
    struct elf64_image_info info={0};
    test_make_image(image,sizeof(image),&header,&program);
    if (!elf64_validate_image(image,sizeof(image),&info) ||
        info.entry!=header.entry || info.load_segment_count!=1U)
        return false;
    if (elf64_validate_image(image,sizeof(header)-1U,&info)) return false;

    header.machine=3U;
    test_store_bytes(image,&header,sizeof(header));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    header.machine=ELF_MACHINE_X86_64;

    program.file_size=program.memory_size+1ULL;
    test_store_bytes(image+(size_t)header.program_header_offset,
                     &program,sizeof(program));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    program.file_size=16ULL;

    program.offset=(uint64_t)sizeof(image)-8ULL;
    test_store_bytes(image+(size_t)header.program_header_offset,
                     &program,sizeof(program));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    program.offset=0x1000ULL;

    program.flags=ELF_FLAG_READ|ELF_FLAG_WRITE|ELF_FLAG_EXECUTE;
    test_store_bytes(image+(size_t)header.program_header_offset,
                     &program,sizeof(program));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    program.flags=ELF_FLAG_READ|ELF_FLAG_EXECUTE;

    program.offset=0x1001ULL;
    test_store_bytes(image+(size_t)header.program_header_offset,
                     &program,sizeof(program));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    program.offset=0x1000ULL;

    header.entry=0x500000ULL;
    test_store_bytes(image,&header,sizeof(header));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    header.entry=0x400000ULL;

    header.program_header_count=2U;
    second=program;
    second.offset=0x1010ULL;
    second.virtual_address=0x400010ULL;
    test_store_bytes(image,&header,sizeof(header));
    test_store_bytes(image+(size_t)header.program_header_offset+
                     sizeof(program),&second,sizeof(second));
    if (elf64_validate_image(image,sizeof(image),&info)) return false;
    return true;
}
#endif
