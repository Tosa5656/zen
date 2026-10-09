void kprint(const char* str, unsigned int row)
{
	volatile char* video_memory = (volatile char*)(0xB8000 + (row*160));

	while(*str != 0)
	{
		*video_memory = *str;
		*(video_memory + 1) = 0x0A;
		video_memory += 2;
		str++;
	}
}

void kmain(void)
{
	kprint("Kernel test", 23);
}
