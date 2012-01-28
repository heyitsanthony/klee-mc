#ifndef MARKOVTEMPLATE_H
#define MARKOVTEMPLATE_H

#include <stdint.h>
#include <map>

template <class T>
class Markov
{
private:
	typedef std::pair<const T*, const T*>	xfer_ty;
	typedef std::map<xfer_ty, uint64_t>	xfer_map_ty;
	// total xfers for a given T *
	typedef std::map<const T*, uint64_t>	xfer_sum_ty;

	xfer_map_ty	xfer_map;
	xfer_sum_ty	xfer_sum;

public:
	Markov(void) {}
	virtual ~Markov(void) {}
	void insert(const T* from, const T* to)
	{
		xfer_ty	xfer(from, to);

		xfer_map[xfer] = xfer_map[xfer] + 1;
		xfer_sum[from] = xfer_sum[from] + 1;
	}

	void insert(T* from, T* to) { insert((const T*)from, (const T*)to); }
	void remove(T* from, T* to) { remove((const T*)from, (const T*)to); }

	void remove(const T* from, const T* to)
	{
		xfer_ty		xfer(from, to);
		uint64_t	map, sum;

		map = xfer_map[xfer];
		sum = xfer_sum[from];

		if (map) xfer_map[xfer] = map - 1;
		if (sum) xfer_sum[from] = sum - 1;
	}

	double getProb(const T* from, const T* to) const
	{
		uint64_t	from_total;

		from_total = getCount(from);
		if (from_total == 0)
			return 0;
		return (double)getCount(from, to) / (double)from_total;
	}

	uint64_t getCount(const T* from, const T* to) const
	{
		typename xfer_map_ty::const_iterator	it;

		it = xfer_map.find(xfer_ty(from, to));
		if (it == xfer_map.end())
			return 0;

		return it->second;
	}

	uint64_t getCount(const T* from) const
	{
		typename xfer_sum_ty::const_iterator	it;

		it = xfer_sum.find(from);
		if (it == xfer_sum.end())
			return 0;

		return it->second;
	}
};

#endif