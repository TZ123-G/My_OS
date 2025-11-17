OUTPUT_ARCH("riscv")
ENTRY(_entry)

SECTIONS
{
  /* 内核将被加载到 0x80000000 (QEMU virt 机器的起始地址) */
  . = 0x80000000;

  .text : {
    /* 确保入口点位于正确位置 */
    *(.text.init)
    *(.text .text.*)
    
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page"); 
    
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    /* 添加 BSS 段的起始和结束符号 */
    _bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    _bss_end = .;
  }

  PROVIDE(end = .);
}