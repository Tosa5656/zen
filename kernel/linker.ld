ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
	. = 0x100000;

	_kernel_start = .;

	.text : ALIGN(4K)
	{
		*(.text)
		*(.text.*)
	}

	.rodata : ALIGN(4K)
	{
		*(.rodata)
		*(.rodata.*)
	}

	.data : ALIGN(4K)
	{
		*(.data)
		*(.data.*)
		*(.data.rel.ro)
		*(.data.rel.ro.*)
		*(.got)
		*(.got.plt)
	}

	.bss : ALIGN(4K)
	{
		*(.bss)
		*(.bss.*)
		*(COMMON)
	}

	_kernel_end = .;
}
