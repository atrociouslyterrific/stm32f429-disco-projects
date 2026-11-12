/* Memory map of the STM32F429ZI: where the program and its variables live. */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}

ENTRY(Reset_Handler)
_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS {
  .text : {
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  _data_load = LOADADDR(.data);
  .data : {
    _data_start = .;
    *(.data*)
    . = ALIGN(4);
    _data_end = .;
  } > RAM AT > FLASH

  .bss : {
    _bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > RAM
}
