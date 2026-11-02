OUTPUT_FORMAT("elf32-littlemips")
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
  . = 0x08804000;
  __executable_start = .;

  .text : {
    *(.text .text.* .gnu.linkonce.t.*)
  } = 0

  .sceStub.text : {
    KEEP(*(.sceStub.text))
    KEEP(*(SORT(.sceStub.text.*)))
  } = 0

  .lib.ent.top : { *(.lib.ent.top) }
  .lib.ent : { KEEP(*(.lib.ent)) }
  .lib.ent.btm : { *(.lib.ent.btm) }
  .lib.stub.top : { *(.lib.stub.top) }
  .lib.stub : { KEEP(*(.lib.stub)) }
  .lib.stub.btm : { *(.lib.stub.btm) }

  .rodata.sceModuleInfo : { KEEP(*(.rodata.sceModuleInfo)) }
  .rodata.sceResident : { KEEP(*(.rodata.sceResident)) }
  .rodata.sceNid : { KEEP(*(.rodata.sceNid)) }
  .rodata.sceVstub : { *(.rodata.sceVstub) }
  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }

  . = ALIGN(16);
  .data : { *(.data .data.* .gnu.linkonce.d.*) }
  _gp = ALIGN(16) + 0x7ff0;
  .got : { *(.got.plt) *(.got) }
  .sdata : { *(.sdata .sdata.* .gnu.linkonce.s.*) }
  .lit8 : { *(.lit8) }
  .lit4 : { *(.lit4) }

  . = ALIGN(16);
  __bss_start = .;
  .sbss : { *(.sbss .sbss.* .scommon) }
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(16);
  _end = .;

  /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.pdr) }
}
