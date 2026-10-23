ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(0x5);  /* R_X */
    data PT_LOAD FLAGS(0x6);  /* R_W */
}

SECTIONS
{
    /* Load everything starting at 1 MiB */
    . = 1M;
    _kernel_start = .;

    .multiboot ALIGN(4) : {
        KEEP(*(.multiboot))
    } :text

    .text ALIGN(4K) :
    {
        *(.text*)
    } :text

    .rodata ALIGN(4K) : { *(.rodata*) } :text
    .data ALIGN(4K) : { *(.data*) } :data
    .bss ALIGN(4K) : { *(.bss*) *(COMMON) } :data

    _kernel_end = .;
}
