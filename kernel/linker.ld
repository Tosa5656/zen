ENTRY(_start)
OUTPUT_FORMAT(binary)

KERNEL_VIRTUAL_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
	. = KERNEL_VIRTUAL_BASE + 0x100000;

	_kernel_start = .;

	.text : AT(ADDR(.text) - KERNEL_VIRTUAL_BASE)
	{
		*(.text)
		*(.text.*)
	}

	.rodata : AT(ADDR(.rodata) - KERNEL_VIRTUAL_BASE)
	{
		*(.rodata)
		*(.rodata.*)
	}

	.data : AT(ADDR(.data) - KERNEL_VIRTUAL_BASE)
	{
		*(.data)
		*(.data.*)
		*(.data.rel.ro)
		*(.data.rel.ro.*)
		*(.got)
		*(.got.plt)
	}

	.bss : AT(ADDR(.bss) - KERNEL_VIRTUAL_BASE)
	{
		*(.bss)
		*(.bss.*)
		*(COMMON)
	}

	_kernel_end = .;
}
