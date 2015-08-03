/**
 * copies out state to another process
 */
#ifndef KLEEMC_HOSTACCEL_H
#define KLEEMC_HOSTACCEL_H

#include "../klee-hw/hw_accel.h"

namespace klee
{
class ExeStateVex;

class HostAccelerator
{
public:
	enum Status {
		HA_NONE,
		HA_PARTIAL,
		HA_SYSCALL,
		HA_CRASHED};

	static HostAccelerator* create();
	virtual ~HostAccelerator();
	Status run(ExeStateVex& esv);
	bool xchk(const ExecutionState& es_hw, ExecutionState& es_klee);
protected:
	HostAccelerator(void);
	void releaseSHM(void);
private:
	void writeSHM(const std::vector<ObjectPair>& objs);
	void readSHM(
		ExeStateVex& s,
		const std::vector<ObjectPair>& objs);
	bool setupSHM(ExeStateVex& s, std::vector<ObjectPair>& objs);
	void stepInstructions(pid_t child_pid);
	void fixupHWShadow(
		const ExecutionState& state, ExecutionState& shadow);

	void setupChild(void);
	void killChild(void);

	int			shm_id;
	unsigned		shm_page_c;
	void			*shm_addr;
	struct hw_map_extent	*shm_maps;
	void			*shm_payload;

	void			*vdso_base;

	unsigned		bad_reg_c,
				partial_run_c,
				full_run_c,
				crashed_kleehw_c,
				badexit_kleehw_c,
				bad_shmget_c;

	int			pipefd[2];
	int			child_pid;

public: /* XXX sshut up warnings for now */
	unsigned		xchk_ok_c;
	unsigned		xchk_miss_c;
	unsigned		xchk_bad_c;
};
}

#endif
