/* Bootloader 专用链接脚本: 仅占前20KB(0x0-0x4FFF), APP在0x5000
 * (docs/bootloader-design.md 分区图)。快编程函数天然位于零等待区。 */
ENTRY( _start )

__stack_size = 1024;

PROVIDE( _stack_size = __stack_size );


MEMORY
{
	FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 20K
	RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 52K
	/* BOINIT: 与APP同址的boot/APP同步noinit区(boot_shared.h) */
	BOINIT (xrw) : ORIGIN = 0x2000D000, LENGTH = 4K
}


SECTIONS
{
	.init :
	{
		_sinit = .;
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.init)))
		. = ALIGN(4);
		_einit = .;
	} >FLASH AT>FLASH

	.vector :
	{
		*(.vector);
		. = ALIGN(64);
	} >FLASH AT>FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata*)
		*(.gnu.linkonce.t.*)
		. = ALIGN(4);
	} >FLASH AT>FLASH

	.fini :
	{
		KEEP(*(SORT_NONE(.fini)))
		. = ALIGN(4);
	} >FLASH AT>FLASH

	PROVIDE( _etext = . );
	PROVIDE( _eitcm = . );

	.dalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_vma = .);
	} >RAM AT>FLASH

	.dlalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_lma = .);
	} >FLASH AT>FLASH

	.data :
	{
		*(.gnu.linkonce.r.*)
		*(.data .data.*)
		*(.gnu.linkonce.d.*)
		. = ALIGN(8);
		PROVIDE( __global_pointer$ = . + 0x800 );
		*(.sdata .sdata.*)
		*(.sdata2.*)
		*(.gnu.linkonce.s.*)
		. = ALIGN(8);
		*(.srodata.cst16)
		*(.srodata.cst8)
		*(.srodata.cst4)
		*(.srodata.cst2)
		*(.srodata .srodata.*)
		. = ALIGN(4);
		PROVIDE( _edata = .);
	} >RAM AT>FLASH

	.bss :
	{
		. = ALIGN(4);
		PROVIDE( _sbss = .);
		*(.sbss*)
		*(.gnu.linkonce.sb.*)
		*(.bss*)
		*(.gnu.linkonce.b.*)
		*(COMMON*)
		. = ALIGN(4);
		PROVIDE( _ebss = .);
	} >RAM AT>FLASH

	/* boot/APP同步区: 绝对地址访问, NOLOAD(启动不清不拷) */
	.boinit (NOLOAD) :
	{
		. = ALIGN(4);
		KEEP(*(.boinit))
	} >BOINIT

	PROVIDE( _end = _ebss);
	PROVIDE( end = . );

	.stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
	{
		PROVIDE( _heap_end = . );
		. = ALIGN(4);
		PROVIDE(_susrstack = . );
		. = . + __stack_size;
		PROVIDE( _eusrstack = .);
	} >RAM

	/* bootloader 20KB 硬边界机械防回退(溢出即链接失败, 而非盖住APP) */
	ASSERT(ADDR(.fini) + SIZEOF(.fini) <= 0x5000, "bootloader溢出20KB分区")
}
