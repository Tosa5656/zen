ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
	. = 0x100000;

	_kernel_start = .;

	.text : ALIGN(4K)
	{
		*(.text)
	}

	.rodata : ALIGN(4K)
	{
		*(.rodata)
	}

	.data : ALIGN(4K)
	{
		*(.data)
	}

	.bss : ALIGN(4K)
	{
		*(.bss)
	}

	_kernel_end = .;
}
