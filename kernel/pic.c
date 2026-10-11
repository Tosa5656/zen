#include "pic.h"
#include "io.h"
#include "serial.h"

void pic_remap(void)
{
	outb(PIC1_COMMAND, 0x11);
	outb(PIC2_COMMAND, 0x11);

	outb(PIC1_DATA, 0x20);
	outb(PIC2_DATA, 0x28);

	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);

	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	outb(PIC1_DATA, 0xFC);
	outb(PIC2_DATA, 0xFF);

	serial_write("PIC inited!\r\n");
}
