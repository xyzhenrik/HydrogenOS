ENTRY(_start)

KERNEL_VMA = 0xFFFF800000000000;
KERNEL_LMA = 0x0000000000100000;

PHDRS
{
    boot PT_LOAD FLAGS(5);     /* R + X */
    bootdata PT_LOAD FLAGS(6); /* R + W */
    text PT_LOAD FLAGS(5);     /* R + X */
    data PT_LOAD FLAGS(6);     /* R + W */
}

SECTIONS
{
    . = KERNEL_LMA;
    _boot_start = .;

    .multiboot :
    {
        *(.multiboot)
    } :boot

    .text.boot :
    {
        *(.text.boot*)
    } :boot

    .rodata.boot :
    {
        *(.rodata.boot*)
    } :boot

    .data.boot :
    {
        *(.data.boot*)
    } :bootdata

    .bss.boot :
    {
        *(.bss.boot*)
    } :bootdata

    . = ALIGN(4K);
    _boot_end = .;

    . = KERNEL_VMA + _boot_end;
    _kernel_start = .;

    .text : AT(_boot_end + (ADDR(.text) - _kernel_start))
    {
        *(.text*)
    } :text

    .rodata : AT(_boot_end + (ADDR(.rodata) - _kernel_start))
    {
        *(.rodata*)
    } :text

    .data : AT(_boot_end + (ADDR(.data) - _kernel_start))
    {
        *(.data*)
    } :data

    .bss : AT(_boot_end + (ADDR(.bss) - _kernel_start))
    {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ :
    {
        *(.eh_frame)
    }

    _kernel_end = .;
    _kernel_lma_start = _boot_end;
    _kernel_lma_end = _boot_end + (_kernel_end - _kernel_start);
}
