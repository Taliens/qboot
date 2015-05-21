#include "bios.h"
#include "linuxboot.h"
#include "string.h"
#include "stdio.h"

static inline uint16_t lduw_p(void *p)
{
	uint16_t val;
	memcpy(&val, p, 2);
	return val;
}

static inline uint32_t ldl_p(void *p)
{
	uint32_t val;
	memcpy(&val, p, 4);
	return val;
}

static inline void stw_p(void *p, uint16_t val)
{
	memcpy(p, &val, 2);
}

static inline void stl_p(void *p, uint32_t val)
{
	memcpy(p, &val, 4);
}

bool parse_bzimage(struct linuxboot_args *args)
{
	uint8_t *header = args->header;

	uint32_t real_addr, cmdline_addr, prot_addr, initrd_addr;
	uint32_t setup_size;
	uint32_t initrd_max;
	uint16_t protocol;

	if (ldl_p(header+0x202) == 0x53726448)
		protocol = lduw_p(header+0x206);
	else {
		// if (parse_multiboot(&args)) return;
		protocol = 0;
	}

	if (protocol < 0x200 || !(header[0x211] & 0x01)) {
		/* Low kernel */
		real_addr    = 0x90000;
		cmdline_addr = (0x9a000 - args->cmdline_size) & ~15;
		prot_addr    = 0x10000;
	} else if (protocol < 0x202) {
		/* High but ancient kernel */
		real_addr    = 0x90000;
		cmdline_addr = (0x9a000 - args->cmdline_size) & ~15;
		prot_addr    = 0x100000;
	} else {
		/* High and recent kernel */
		real_addr    = 0x10000;
		cmdline_addr = 0x20000;
		prot_addr    = 0x100000;
	}

	if (protocol >= 0x203)
		initrd_max = ldl_p(header+0x22c);
	else
		initrd_max = 0x37ffffff;

	if (protocol >= 0x202)
		stl_p(header+0x228, cmdline_addr);
	else {
		stw_p(header+0x20, 0xA33F);
		stw_p(header+0x22, cmdline_addr-real_addr);
	}

	/* High nybble = B reserved for QEMU; low nybble is revision number.
	 * If this code is substantially changed, you may want to consider
	 * incrementing the revision. */
	if (protocol >= 0x200)
		header[0x210] = 0xB0;

	/* heap */
	if (protocol >= 0x201) {
		header[0x211] |= 0x80;  /* CAN_USE_HEAP */
		stw_p(header+0x224, cmdline_addr-real_addr-0x200);
	}

	if (args->initrd_size)
		initrd_addr = (initrd_max - args->initrd_size) & ~4095;
	else
		initrd_addr = 0;
	stl_p(header+0x218, initrd_addr);
	stl_p(header+0x21c, args->initrd_size);

	/* load kernel and setup */
	setup_size = header[0x1f1];
	if (setup_size == 0)
		setup_size = 4;

	args->setup_size = (setup_size+1)*512;
	args->kernel_size = args->vmlinuz_size - setup_size;
	args->initrd_addr = (void *)initrd_addr;
	args->setup_addr = (void *)real_addr;
	args->kernel_addr = (void *)prot_addr;
	args->cmdline_addr = (void *)cmdline_addr;
	return true;
}

void boot_bzimage(struct linuxboot_args *args)
{
	memcpy(args->setup_addr, args->header, sizeof(args->header));
	asm volatile(
	    "ljmp $0x18, $pm16_boot_linux - 0xf0000"
	    : :
	    "b" (((uintptr_t) args->setup_addr) >> 4),
	    "d" (args->cmdline_addr - args->setup_addr - 16));
        panic();
}

/* BX = address of data block
 * DX = cmdline_addr-setup_addr-16
 */
asm("pm16_boot_linux:"
	    ".code16;"
	    "mov $0, %eax; mov %eax, %cr0;"
	    "ljmpl $0xf000, $(1f - 0xf0000); 1:"
	    "mov %bx, %ds; mov %bx, %es;"
	    "mov %bx, %fs; mov %bx, %gs; mov %bx, %ss;"
	    "mov %dx, %sp;"
	    "add $0x20, %bx; pushw %bx;"    // push CS
	    "xor %eax, %eax; pushw %ax;"    // push IP
	    "xor %ebx, %ebx;"
	    "xor %ecx, %ecx;"
	    "xor %edx, %edx;"
	    "xor %edi, %edi;"
	    "xor %ebp, %ebp;"
	    "lret;"
	    ".code32");
