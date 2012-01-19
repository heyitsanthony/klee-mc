#ifndef GUESTPRIORITIZER_H
#define GUESTPRIORITIZER_H

namespace klee
{
class GuestPrioritizer : public Prioritizer
{
public:
	GuestPrioritizer(ExecutorVex& in_exe)
	: exe(in_exe)
	, len(0x400000) /* 4MB of code should be enough! */
	{
		const Guest	*gs = exe.getGuest();
		//base = gs->getEntryPoint().o & ~((uint64_t)len - 1);
		std::cerr << "GUESTPRIORITIZER: FORCING 4MB REGION\n";
		base = 0x400000;
		end = base + 2*len;
	}

	virtual Prioritizer* copy(void) const
	{ return new GuestPrioritizer(exe); }

	virtual ~GuestPrioritizer() {}

	int getPriority(ExecutionState& st)
	{
		const VexSB	*vsb;
		KInstruction	*ki = st.pc;

		if (ki == NULL)
			return 1;

		vsb = exe.getFuncVSB(ki->getInst()->getParent()->getParent());
		if (vsb == NULL)
			return 0;

		if (	(uint64_t)vsb->getGuestAddr().o < base ||
			(uint64_t)vsb->getGuestAddr().o > end)
			return 0;

		return 1;
	}
private:
	ExecutorVex	&exe;
	uint64_t	base, end;
	unsigned int	len;
};
}
#endif
