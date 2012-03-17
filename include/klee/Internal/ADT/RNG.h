//===-- RNG.h ---------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_UTIL_RNG_H
#define KLEE_UTIL_RNG_H

#include <vector>
#include <assert.h>

namespace klee {
class RNG {
private:
	/* Period parameters */
	static const int N = 624;
	static const int M = 397;

	/* constant vector a */
	static const unsigned int MATRIX_A = 0x9908b0dfUL;
	/* most significant w-r bits */
	static const unsigned int UPPER_MASK = 0x80000000UL;
	/* least significant r bits */
	static const unsigned int LOWER_MASK = 0x7fffffffUL;

private:
	unsigned int mt[N]; /* the array for the state vector  */
	int mti;

public:
	RNG(unsigned int seed=0UL, bool do_seed=true);
	virtual ~RNG() {}

	void seed(unsigned int seed);

	/* generates a random number on [0,0xffffffff]-interval */
	virtual unsigned int getInt32();
	/* generates a random number on [0,0x7fffffff]-interval */
	int getInt31();
	/* generates a random number on [0,1]-real-interval */
	double getDoubleLR();
	float getFloatLR();
	/* generates a random number on [0,1)-real-interval */
	double getDoubleL();
	float getFloatL();
	/* generates a random number on (0,1)-real-interval */
	double getDouble();
	float getFloat();
	/* generators a random flop */
	bool getBool();
};

class ReplayRNG : public RNG
{
public:
	ReplayRNG(const std::vector<unsigned int>& v)
	: RNG(0, false), log(v), off(0) {}
	ReplayRNG() : RNG(0, false), off(0) {}
	void add(unsigned int v) { log.push_back(v); }
	virtual unsigned getInt32(void)
	{
		assert (off < log.size());
		return log[off++];
	}
	virtual ~ReplayRNG(void) {}
private:
	std::vector<unsigned int>	log;
	unsigned			off;
};

class LoggingRNG : public RNG
{
public:
	LoggingRNG(unsigned int seed=0UL) : RNG(seed) {}
	virtual ~LoggingRNG() {}
	ReplayRNG* getReplay(void) { return new ReplayRNG(log); }
	virtual unsigned int getInt32(void)
	{
		unsigned int ret = RNG::getInt32();
		log.push_back(ret);
		return ret;
	}
private:
	std::vector<unsigned int> log;
};

}

#endif
