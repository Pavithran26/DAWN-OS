/* SPDX-License-Identifier: Unlicense */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD FLAGS(6); /* read + write: Limine updates request responses */
    text PT_LOAD FLAGS(5);     /* read + execute */
    rodata PT_LOAD FLAGS(4);   /* read only */
    data PT_LOAD FLAGS(6);     /* read + write */
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;

    /* Keep protocol requests in a dedicated, page-aligned load segment. */
    .limine_requests :
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .text :
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata :
    {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data :
    {
        *(.data .data.*)
    } :data

    .bss (NOLOAD) :
    {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    __kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
        *(.comment)
    }
}
