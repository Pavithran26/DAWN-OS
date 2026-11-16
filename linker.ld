/* SPDX-License-Identifier: Unlicense */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* read + execute */
    rodata PT_LOAD FLAGS(4); /* read only */
    data PT_LOAD FLAGS(6);   /* read + write */
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;

    .text ALIGN(0x1000) :
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    .rodata ALIGN(0x1000) :
    {
        *(.rodata .rodata.*)
    } :rodata

    .limine_requests ALIGN(8) :
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    .data ALIGN(0x1000) :
    {
        *(.data .data.*)
    } :data

    .bss ALIGN(0x1000) (NOLOAD) :
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
