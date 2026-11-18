/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "elf_loader.h"
#include "page_table.h"
#include "physical_memory.h"
#include "process.h"

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

#define ELF64_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

static inline uint64_t elf64_read_rflags(void) {
    uint64_t flags=0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}
static inline uint16_t elf64_read_cs(void) {
    uint16_t selector=0U;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}
static inline void elf64_restore_interrupt_flag(uint64_t flags) {
    if ((flags&ELF64_RFLAGS_INTERRUPT_ENABLE)!=0ULL)
        __asm__ volatile ("sti" : : : "memory");
}

bool elf64_load_process(uint64_t process_id,const void *image,
                        size_t image_size,
                        struct elf64_loaded_process *loaded) {
    const uint64_t saved_flags=elf64_read_rflags();
    const uint8_t *bytes=(const uint8_t *)image;
    struct elf64_header header={0};
    struct elf64_image_info validated={0};
    struct process_info process={0};
    size_t checkpoint=0U;
    uint64_t page_total=(uint64_t)DAWN_ELF64_STACK_PAGES;
    const uint64_t stack_floor=DAWN_ELF64_STACK_TOP-
        (uint64_t)DAWN_ELF64_STACK_PAGES*ELF64_PAGE_SIZE;

    if ((elf64_read_cs()&3U)!=0U) return false;
    __asm__ volatile ("cli" : : : "memory");
    if (loaded==NULL || loaded->entry!=0ULL ||
        loaded->user_stack_top!=0ULL ||
        loaded->load_segment_count!=0U || loaded->mapped_page_count!=0U ||
        !elf64_validate_image(image,image_size,&validated) ||
        !process_get_info(process_id,&process) ||
        process.state!=PROCESS_STATE_CREATED ||
        !process_user_page_checkpoint(process_id,&checkpoint) ||
        checkpoint!=0U || image_size<sizeof(header)) {
        elf64_restore_interrupt_flag(saved_flags);
        return false;
    }

    copy_bytes(&header,bytes,sizeof(header));
    for (uint16_t index=0U;index<header.program_header_count;index++) {
        struct elf64_program_header program={0};
        uint64_t end=0ULL,page_start=0ULL,page_end=0ULL,pages=0ULL;
        const uint64_t phoff=header.program_header_offset+
            (uint64_t)index*(uint64_t)sizeof(program);
        copy_bytes(&program,bytes+(size_t)phoff,sizeof(program));
        if (program.type!=ELF_PROGRAM_LOAD) continue;
        end=program.virtual_address+program.memory_size;
        page_start=program.virtual_address&~(ELF64_PAGE_SIZE-1ULL);
        page_end=(end+ELF64_PAGE_SIZE-1ULL)&~(ELF64_PAGE_SIZE-1ULL);
        if (page_end>stack_floor || page_end<=page_start) {
            elf64_restore_interrupt_flag(saved_flags);
            return false;
        }
        pages=(page_end-page_start)/ELF64_PAGE_SIZE;
        if (pages>DAWN_ELF64_MAX_IMAGE_PAGES-page_total) {
            elf64_restore_interrupt_flag(saved_flags);
            return false;
        }
        page_total+=pages;
    }
    if (page_total>DAWN_ELF64_MAX_IMAGE_PAGES) {
        elf64_restore_interrupt_flag(saved_flags);
        return false;
    }

    for (uint16_t index=0U;index<header.program_header_count;index++) {
        struct elf64_program_header program={0};
        uint64_t end=0ULL,page_start=0ULL,page_end=0ULL;
        uint32_t mapping_flags=(uint32_t)VM_PAGE_USER;
        const uint64_t phoff=header.program_header_offset+
            (uint64_t)index*(uint64_t)sizeof(program);
        copy_bytes(&program,bytes+(size_t)phoff,sizeof(program));
        if (program.type!=ELF_PROGRAM_LOAD) continue;
        end=program.virtual_address+program.memory_size;
        page_start=program.virtual_address&~(ELF64_PAGE_SIZE-1ULL);
        page_end=(end+ELF64_PAGE_SIZE-1ULL)&~(ELF64_PAGE_SIZE-1ULL);
        if ((program.flags&ELF_FLAG_WRITE)!=0U)
            mapping_flags|=(uint32_t)VM_PAGE_WRITE;
        if ((program.flags&ELF_FLAG_EXECUTE)!=0U)
            mapping_flags|=(uint32_t)VM_PAGE_EXECUTE;

        for (uint64_t page=page_start;page<page_end;
             page+=ELF64_PAGE_SIZE) {
            if (!process_allocate_user_page(process_id,page,mapping_flags))
                goto rollback;
        }
        /* Fresh user pages are not assumed to be zero by the physical allocator. */
        if (!process_user_memory_zero(process_id,page_start,
                                      (size_t)(page_end-page_start)))
            goto rollback;
        if (program.file_size>0ULL &&
            !process_user_memory_write(process_id,program.virtual_address,
                bytes+(size_t)program.offset,(size_t)program.file_size))
            goto rollback;
        if (program.memory_size>program.file_size &&
            !process_user_memory_zero(process_id,
                program.virtual_address+program.file_size,
                (size_t)(program.memory_size-program.file_size)))
            goto rollback;
    }

    for (uint64_t index=0ULL;index<(uint64_t)DAWN_ELF64_STACK_PAGES;index++) {
        const uint64_t page=DAWN_ELF64_STACK_TOP-
            (index+1ULL)*ELF64_PAGE_SIZE;
        if (!process_allocate_user_page(process_id,page,
                (uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE))
            goto rollback;
    }
    if (!process_user_memory_zero(process_id,stack_floor,
            (size_t)((uint64_t)DAWN_ELF64_STACK_PAGES*ELF64_PAGE_SIZE)))
        goto rollback;
    if (!process_mark_ready(process_id)) goto rollback;

    loaded->entry=validated.entry;
    loaded->user_stack_top=DAWN_ELF64_STACK_TOP;
    loaded->load_segment_count=validated.load_segment_count;
    loaded->mapped_page_count=(uint16_t)page_total;
    elf64_restore_interrupt_flag(saved_flags);
    return true;

rollback:
    (void)process_user_memory_rollback(process_id,checkpoint);
    elf64_restore_interrupt_flag(saved_flags);
    return false;
}

bool elf64_setup_user_stack(uint64_t process_id,uint64_t *inout_stack_top,
                            int argc,const char *const argv[],const char *const envp[],
                            uint64_t *out_rdi,uint64_t *out_rsi,uint64_t *out_rdx) {
    uint64_t sp=0ULL;
    uint64_t argv_ptrs[64]={0ULL};
    uint64_t envp_ptrs[64]={0ULL};
    int effective_argc=0;
    int envc=0;
    uint64_t table_size=0ULL;
    uint64_t target_sp=0ULL;
    const uint64_t stack_floor=DAWN_ELF64_STACK_TOP-
        (uint64_t)DAWN_ELF64_STACK_PAGES*ELF64_PAGE_SIZE;

    if (inout_stack_top==NULL || *inout_stack_top==0ULL ||
        *inout_stack_top>DAWN_ELF64_STACK_TOP ||
        *inout_stack_top<stack_floor+ELF64_PAGE_SIZE)
        return false;

    sp=*inout_stack_top;
    if (argc>0 && argv!=NULL) {
        effective_argc=argc>64 ? 64 : argc;
    }
    if (envp!=NULL) {
        while (envp[envc]!=NULL && envc<64) ++envc;
    }

    for (int i=envc-1;i>=0;i--) {
        size_t len=0U;
        if (envp[i]==NULL) continue;
        while (envp[i][len]!='\0' && len<4096U) len++;
        len++;
        if (sp<stack_floor+ELF64_PAGE_SIZE+len) return false;
        sp-=len;
        if (!process_user_memory_write(process_id,sp,envp[i],len)) return false;
        envp_ptrs[i]=sp;
    }

    for (int i=effective_argc-1;i>=0;i--) {
        size_t len=0U;
        if (argv[i]==NULL) continue;
        while (argv[i][len]!='\0' && len<4096U) len++;
        len++;
        if (sp<stack_floor+ELF64_PAGE_SIZE+len) return false;
        sp-=len;
        if (!process_user_memory_write(process_id,sp,argv[i],len)) return false;
        argv_ptrs[i]=sp;
    }

    const uint64_t total_ptrs=(uint64_t)(3U+(uint32_t)effective_argc+(uint32_t)envc);
    table_size=total_ptrs*8U;
    if (sp<stack_floor+ELF64_PAGE_SIZE+table_size) return false;

    target_sp=(sp-table_size)&~(uint64_t)0x0fU;
    sp=target_sp+table_size;

    sp-=8U;
    {
        uint64_t null_val=0ULL;
        if (!process_user_memory_write(process_id,sp,&null_val,sizeof(null_val))) return false;
    }
    for (int i=envc-1;i>=0;i--) {
        sp-=8U;
        if (!process_user_memory_write(process_id,sp,&envp_ptrs[i],sizeof(uint64_t))) return false;
    }
    if (out_rdx!=NULL) *out_rdx=(envc>0) ? sp : 0ULL;

    sp-=8U;
    {
        uint64_t null_val=0ULL;
        if (!process_user_memory_write(process_id,sp,&null_val,sizeof(null_val))) return false;
    }
    for (int i=effective_argc-1;i>=0;i--) {
        sp-=8U;
        if (!process_user_memory_write(process_id,sp,&argv_ptrs[i],sizeof(uint64_t))) return false;
    }
    if (out_rsi!=NULL) *out_rsi=(effective_argc>0) ? sp : 0ULL;

    sp-=8U;
    {
        uint64_t argc_val=(uint64_t)(uint32_t)effective_argc;
        if (!process_user_memory_write(process_id,sp,&argc_val,sizeof(argc_val))) return false;
    }
    if (out_rdi!=NULL) *out_rdi=(uint64_t)(uint32_t)effective_argc;

    *inout_stack_top=sp;
    return true;
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
            if (current.type==ELF_PROGRAM_GNU_STACK &&
                ((current.flags&~(uint32_t)ELF_FLAG_MASK)!=0U ||
                 (current.flags&ELF_FLAG_EXECUTE)!=0U))
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

bool elf64_build_init_image(void *buffer,size_t capacity,
                            size_t *image_size) {
    static const uint8_t init_code[]={
        0x48U,0xc7U,0xc0U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc7U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc6U,0x43U,0x00U,0x40U,0x00U,
        0x48U,0xc7U,0xc2U,0x1bU,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0xc7U,0xc0U,0x03U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0xc7U,0xc0U,0x04U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0xc7U,0xc0U,0x02U,0x00U,0x00U,0x00U,
        0x48U,0x31U,0xffU,
        0xcdU,0x80U,
        0x0fU,0x0bU,
        '[','D','A','W','N','-','I','N','I','T',']',' ','P','I','D',' ','1',' ','a','c','t','i','v','e','.','\n','\0'
    };
    uint8_t *bytes=(uint8_t *)buffer;
    struct elf64_header header={0};
    struct elf64_program_header program={0};
    const size_t total_size=0x1000U+sizeof(init_code);

    if (buffer==NULL || image_size==NULL || capacity<total_size)
        return false;
    for (size_t i=0U;i<total_size;i++) bytes[i]=0U;

    header.ident[0]=0x7fU;
    header.ident[1]=(uint8_t)'E';
    header.ident[2]=(uint8_t)'L';
    header.ident[3]=(uint8_t)'F';
    header.ident[4]=ELF_IDENT_CLASS_64;
    header.ident[5]=ELF_IDENT_DATA_LSB;
    header.ident[6]=ELF_IDENT_VERSION;
    header.type=ELF_TYPE_EXEC;
    header.machine=ELF_MACHINE_X86_64;
    header.version=ELF_VERSION_CURRENT;
    header.entry=0x400000ULL;
    header.program_header_offset=sizeof(struct elf64_header);
    header.header_size=(uint16_t)sizeof(struct elf64_header);
    header.program_header_entry_size=(uint16_t)sizeof(struct elf64_program_header);
    header.program_header_count=1U;

    program.type=ELF_PROGRAM_LOAD;
    program.flags=ELF_FLAG_READ|ELF_FLAG_EXECUTE;
    program.offset=0x1000ULL;
    program.virtual_address=0x400000ULL;
    program.file_size=(uint64_t)sizeof(init_code);
    program.memory_size=(uint64_t)sizeof(init_code);
    program.alignment=ELF64_PAGE_SIZE;

    copy_bytes(bytes,&header,sizeof(header));
    copy_bytes(bytes+sizeof(header),&program,sizeof(program));
    copy_bytes(bytes+0x1000U,init_code,sizeof(init_code));

    *image_size=total_size;
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

bool elf64_build_test_exit_image(void *buffer,size_t capacity,
                                    size_t *image_size) {
    static const uint8_t exit_code[]={
        /* GETPID must return a non-zero active process identifier. */
        0x48U,0xc7U,0xc0U,0x03U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x85U,0xc0U,0x75U,0x02U,0x0fU,0x0bU,
        /* YIELD returns zero after the scheduler resumes this task. */
        0x48U,0xc7U,0xc0U,0x04U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x85U,0xc0U,0x74U,0x02U,0x0fU,0x0bU,
        /* Bounded CPL3 spin gives the LAPIC timer a chance to run the READY peer. */
        0xb9U,0x00U,0x00U,0x00U,0x10U,
        0xffU,0xc9U,0x75U,0xfcU,
        0x48U,0xc7U,0xc0U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc7U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc6U,0x5bU,0x00U,0x40U,0x00U,
        0x48U,0xc7U,0xc2U,0x0dU,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x83U,0xf8U,0x0dU,
        0x75U,0x0cU,
        0x48U,0xc7U,0xc0U,0x02U,0x00U,0x00U,0x00U,
        0x48U,0x31U,0xffU,
        0xcdU,0x80U,
        0x0fU,0x0bU,
        'D','A','W','N','-','U','S','E','R','-','O','K','\n'
    };
    struct elf64_header header={0};
    struct elf64_program_header program={0};
    uint8_t *image=(uint8_t *)buffer;
    if (image==NULL || image_size==NULL ||
        capacity<0x1000U+sizeof(exit_code)) return false;
    test_make_image(image,capacity,&header,&program);
    program.file_size=sizeof(exit_code);
    program.memory_size=ELF64_PAGE_SIZE;
    program.flags=ELF_FLAG_READ|ELF_FLAG_EXECUTE;
    test_store_bytes(image+(size_t)header.program_header_offset,
                     &program,sizeof(program));
    test_store_bytes(image+(size_t)program.offset,
                     exit_code,sizeof(exit_code));
    *image_size=(size_t)program.offset+sizeof(exit_code);
    return true;
}

static bool elf64_process_load_self_test(void) {
    uint8_t image[8192]={0U},bad_image[8192]={0U};
    uint8_t observed_code[128]={0U},observed_bss[32];
    struct elf64_loaded_process loaded={0};
    struct process_info info={0};
    struct vm_address_space space={0};
    struct physical_allocator_stats before={0},after={0};
    uint64_t process_id=0ULL,physical=0ULL;
    uint32_t code_flags=0U,stack_flags=0U;
    size_t image_size=0U;
    bool created=false,cleaned=false,result=false;
    static const uint8_t expected_code[]={
        /* GETPID must return a non-zero active process identifier. */
        0x48U,0xc7U,0xc0U,0x03U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x85U,0xc0U,0x75U,0x02U,0x0fU,0x0bU,
        /* YIELD returns zero after the scheduler resumes this task. */
        0x48U,0xc7U,0xc0U,0x04U,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x85U,0xc0U,0x74U,0x02U,0x0fU,0x0bU,
        0xb9U,0x00U,0x00U,0x00U,0x10U,
        0xffU,0xc9U,0x75U,0xfcU,
        0x48U,0xc7U,0xc0U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc7U,0x01U,0x00U,0x00U,0x00U,
        0x48U,0xc7U,0xc6U,0x5bU,0x00U,0x40U,0x00U,
        0x48U,0xc7U,0xc2U,0x0dU,0x00U,0x00U,0x00U,
        0xcdU,0x80U,
        0x48U,0x83U,0xf8U,0x0dU,
        0x75U,0x0cU,
        0x48U,0xc7U,0xc0U,0x02U,0x00U,0x00U,0x00U,
        0x48U,0x31U,0xffU,
        0xcdU,0x80U,
        0x0fU,0x0bU,
        'D','A','W','N','-','U','S','E','R','-','O','K','\n'
    };
    physical_allocator_get_stats(&before);
    if (!elf64_build_test_exit_image(image,sizeof(image),&image_size))
        return false;
    copy_bytes(bad_image,image,image_size);
    bad_image[0]=0U;
    if (!process_create(&process_id)) return false;
    created=true;
    if (elf64_load_process(process_id,bad_image,image_size,&loaded) ||
        loaded.entry!=0ULL || loaded.mapped_page_count!=0U)
        goto cleanup;
    if (!elf64_load_process(process_id,image,image_size,&loaded) ||
        loaded.entry!=0x400000ULL ||
        loaded.user_stack_top!=DAWN_ELF64_STACK_TOP ||
        loaded.load_segment_count!=1U ||
        loaded.mapped_page_count!=
            1U+DAWN_ELF64_STACK_PAGES ||
        !process_get_info(process_id,&info) ||
        info.state!=PROCESS_STATE_READY)
        goto cleanup;
    space.root_physical=info.address_space_root;
    if (!vm_address_space_translate(&space,loaded.entry,&physical,&code_flags) ||
        (code_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (code_flags&(uint32_t)VM_PAGE_EXECUTE)==0U ||
        (code_flags&(uint32_t)VM_PAGE_WRITE)!=0U ||
        !vm_address_space_read(&space,observed_code,loaded.entry,
                               sizeof(observed_code)))
        goto cleanup;
    for (size_t index=0U;index<sizeof(expected_code);index++)
        if (observed_code[index]!=expected_code[index]) goto cleanup;
    for (size_t index=0U;index<sizeof(observed_bss);index++)
        observed_bss[index]=0xffU;
    if (!vm_address_space_read(&space,observed_bss,
            loaded.entry+(uint64_t)sizeof(expected_code),
            sizeof(observed_bss))) goto cleanup;
    for (size_t index=0U;index<sizeof(observed_bss);index++)
        if (observed_bss[index]!=0U) goto cleanup;
    if (!vm_address_space_translate(&space,loaded.user_stack_top-1ULL,
                                    &physical,&stack_flags) ||
        (stack_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (stack_flags&(uint32_t)VM_PAGE_WRITE)==0U ||
        (stack_flags&(uint32_t)VM_PAGE_EXECUTE)!=0U)
        goto cleanup;

    {
        const char *test_argv[]={"test_prog","arg1",NULL};
        const char *test_envp[]={"ENV=1",NULL};
        uint64_t stack_top=loaded.user_stack_top;
        uint64_t rdi=0ULL,rsi=0ULL,rdx=0ULL;
        uint64_t read_argc=0ULL;
        if (!elf64_setup_user_stack(process_id,&stack_top,2,test_argv,test_envp,&rdi,&rsi,&rdx) ||
            rdi!=2ULL || rsi==0ULL || rdx==0ULL || (stack_top&0x0fULL)!=0ULL)
            goto cleanup;
        if (!vm_address_space_read(&space,&read_argc,stack_top,sizeof(read_argc)) ||
            read_argc!=2ULL)
            goto cleanup;
    }

    result=true;
cleanup:
    if (created) {
        if (process_mark_exited(process_id) &&
            process_reap(process_id)) cleaned=true;
        else result=false;
    }
    physical_allocator_get_stats(&after);
    return result && cleaned &&
           after.free_frames==before.free_frames;
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
    return elf64_process_load_self_test();
}
#endif
