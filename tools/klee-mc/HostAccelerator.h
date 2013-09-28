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
		HA_SYSCALL };

	static HostAccelerator* create();
	virtual ~HostAccelerator();
	Status run(ExeStateVex& esv);
protected:
	HostAccelerator(void);
	void releaseSHM(void);
private:
	void writeSHM(const std::vector<ObjectPair>& objs);
	void readSHM(
		ExeStateVex& s,
		const std::vector<ObjectPair>& objs);
	void setupSHM(ExeStateVex& s, std::vector<ObjectPair>& objs);

	void stepInstructions(pid_t child_pid);

	int			shm_id;
	unsigned		shm_page_c;
	void			*shm_addr;
	struct hw_map_extent	*shm_maps;
	void			*shm_payload;

	unsigned		bad_reg_c, partial_run_c, full_run_c;

};
}

#endif
