OUTPUT_ARCH(riscv)

ENTRY(_start)

SECTIONS
{
	. = 0xffffffff80000000;

	.text : {
		*(.head.text)
		*(.text .text.*)
	}

	.rodata : {
		*(.rodata .rodata.*)
	}

	.data : {
		*(.data .data.*)
	}

	.bss : {
		*(.bss .bss.*)
	}
}
