#ifndef KLEE_FIXEDSTACK_H
#define KLEE_FIXEDSTACK_H

namespace klee
{
template<typename T>
class FixedStack
{
	unsigned pos, max;
	T *elts;

public:
	FixedStack(unsigned _max)
	: pos(0)
	, max(_max)
	, elts(new T[max]) {}

	FixedStack(const FixedStack &b)
	: pos(b.pos)
	, max(b.max)
	, elts(new T[b.max])
	{ std::copy(b.elts, b.elts+pos, elts); }

	~FixedStack() { delete[] elts; }

	void push_back(const T &elt) { elts[pos++] = elt; }
	void pop_back() { --pos; }
	bool empty() { return pos==0; }
	T &back() { return elts[pos-1]; }

	FixedStack &operator=(const FixedStack &b)
	{
		assert(max == b.max); 
		pos = b.pos;
		std::copy(b.elts, b.elts+pos, elts);
		return *this;
	}

	bool operator==(const FixedStack &b)
	{ return (pos == b.pos && std::equal(elts, elts+pos, b.elts)); 	}

	bool operator!=(const FixedStack &b) { return !(*this==b); }
};

}

#endif
