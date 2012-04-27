/* converts output from info registers into regfile for klee-mc guest */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <valgrind/libvex_guest_amd64.h>

#define VEX_OFF(x)	offsetof(VexGuestAMD64State, x)

struct xlate_ent
{
	const char*	regname;
	uint64_t	vex_byte_off;
};

struct xlate_ent xlate_tab[] = {
{ "rax", VEX_OFF(guest_RAX) },
{ "rbx", VEX_OFF(guest_RBX) },
{ "rcx", VEX_OFF(guest_RCX) },
{ "rdx", VEX_OFF(guest_RDX) },
{ "rsi", VEX_OFF(guest_RSI) },
{ "rdi", VEX_OFF(guest_RDI) },
{ "rbp", VEX_OFF(guest_RBP) },
{ "rsp", VEX_OFF(guest_RSP) },
{ "r8" , VEX_OFF(guest_R8) },
{ "r9" , VEX_OFF(guest_R9) },
{ "r10", VEX_OFF(guest_R10) },
{ "r11", VEX_OFF(guest_R11) },
{ "r12", VEX_OFF(guest_R12) },
{ "r13", VEX_OFF(guest_R13) },
{ "r14", VEX_OFF(guest_R14) },
{ "r15", VEX_OFF(guest_R15) },
{ "rip", VEX_OFF(guest_RIP) },
{ NULL, 0}
};


int main(int argc, char* argv[])
{
	FILE			*f;
	char			*out_buf;
	char			namebuf[32];
	VexGuestAMD64State	*vgs;
	uint64_t		v1;
	unsigned		i;

	f = fopen(argv[1], "rb");
	assert (f != NULL);
	out_buf = malloc(633);
	vgs = (void*)out_buf;
	fread(out_buf, 633, 1, f);
	fclose(f);

	f = fopen(argv[2], "r");

	for (i = 0; xlate_tab[i].regname; i++) {
		char	linebuf[512];
		fgets(linebuf, 512, f);
		sscanf(linebuf, "%32s %lx", namebuf, &v1);
		assert (strcmp(namebuf, xlate_tab[i].regname) == 0);
		fprintf(stderr, "HEY %p\n", (void*)v1);
		*((uint64_t*)(out_buf + xlate_tab[i].vex_byte_off)) = v1;
	}

	/* eflags */
	fscanf(f, "%32s %lx", namebuf, &v1);
	assert (strcmp(namebuf, "eflags") == 0);
	vgs->guest_ACFLAG = (v1 & (1 << 18)) ? 1 : 0;
	vgs->guest_IDFLAG = (v1 & (1 << 21)) ? 1 : 0;
	vgs->guest_DFLAG = (v1 & 0x400) ? -1 : 1;


	fwrite(out_buf, 633, 1, stdout);

	return 0;
}