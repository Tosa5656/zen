#include "pic.h"
#include "io.h"
#include "serial.h"

void pic_remap(void)
{
	outb(PIC1_COMMAND, 0x11);
	outb(PIC2_COMMAND, 0x11);

	outb(PIC1_DATA, PIC1_OFFSET);
	outb(PIC2_DATA, PIC2_OFFSET);

	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);

	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	outb(PIC1_DATA, 0xFC);
	outb(PIC2_DATA, 0xFF);

	serial_write("PIC inited!\r\n");
}

void pic_send_eoi(uint8_t irq)
{
	if (irq >= 8)
	{
		outb(PIC2_COMMAND, PIC_EOI);
	}
	outb(PIC1_COMMAND, PIC_EOI);
}

void pic_disable(void)
{
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}
