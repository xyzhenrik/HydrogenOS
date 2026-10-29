ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R + X */
    data PT_LOAD FLAGS(6); /* R + W */
}

SECTIONS
{
    . = 0x0000000080000000;

    .text :
    {
        *(.text*)
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    /* The loader sets page rights per page: data must not share a page with
       text/rodata, or that page would end up writable and executable. */
    . = ALIGN(0x1000);

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data
}
