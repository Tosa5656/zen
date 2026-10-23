#ifndef PSF_H
#define PSF_H

#define PSF2_MAGIC 0x864AB572

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t headersize;
	uint32_t flags;
	uint32_t numglyph;
	uint32_t bytesperglyph;
	uint32_t height;
	uint32_t width;
} __attribute__((packed)) psf2_header_t;

#endif
