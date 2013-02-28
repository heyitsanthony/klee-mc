/* a prototyped pointer is a pointer
 * which implements the prototyping pattern.
 * The reason is that sometimes you want a pointer field which
 * is deep-copied on a copy-constructor */
#ifndef PROTOPTR_H
#define PROTOPTR_H

namespace klee
{
template <class T>
class ProtoPtr
{
public:
	ProtoPtr() : t(0) {}
	ProtoPtr(const T& _t) : t(_t.copy()) {}
	~ProtoPtr() { if (t) delete t; }
	ProtoPtr(const ProtoPtr& p) : t((!p.t) ? 0 : p.t->copy()) {}
	T* get(void) const { return t; }
	ProtoPtr<T>& operator =(const ProtoPtr<T>& p)
	{ if (t) delete t; t = (p.t) ? p.t->copy() : 0; return *this; }
private:
	T*	t;
};
}
#endif
