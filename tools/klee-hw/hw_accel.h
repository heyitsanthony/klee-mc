#ifndef HWACCEL_PKTS_H
#define HWACCEL_PKTS_H

struct shm_pkt
{
	int		sp_shmid;
	unsigned	sp_pages;
};

struct hw_map_extent
{
	void		*hwm_addr;
	unsigned	hwm_bytes;
	unsigned	hwm_prot;
};

#endif
