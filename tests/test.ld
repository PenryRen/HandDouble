ENTRY(_start)
MEMORY { CODE (rx) : ORIGIN = 0x10000, LENGTH = 1M
         RAM (rw) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text.start) *(.text*) *(.rodata*) } > CODE
 .data : { *(.data*) *(.sdata*) } > RAM
 .bss : { *(.bss*) *(.sbss*) *(COMMON) } > RAM
}
