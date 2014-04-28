#ifndef KMC_CRUMBS_H
#define KMC_CRUMBS_H

#include <iostream>
#include <set>
#include "klee/breadcrumb.h"

namespace klee
{
class KTestStream;
class BCrumb;

/* convenient way to read breadcrumb files */
class Crumbs
{
public:
	virtual ~Crumbs();

	static Crumbs* create(const char* fname);
	static Crumbs* createEmpty(void) { return create("/dev/null"); }

	struct breadcrumb* next(void);
	struct breadcrumb* next(unsigned int bc_type);
	struct breadcrumb* peek(void);

	BCrumb* nextBC(void) { return toBC(next()); }
	BCrumb* nextBC(unsigned int bc_type) { return toBC(next(bc_type)); }
	BCrumb* peekBC(void) { return toBC(peek()); }

	void skip(unsigned int i = 1);

	bool hasType(unsigned int v) const;
	unsigned int getNumProcessed(void) const { return crumbs_processed; }
	static void freeCrumb(struct breadcrumb* bs);
	static BCrumb *toBC(struct breadcrumb* bc);

	Crumbs* copy(void) const { return create(path.c_str()); }
protected:
	Crumbs(const char* fname);
private:
	std::istream			*getFile(const char* fname);
	void 				loadTypeList(std::istream& is);
	std::istream			*is;
	std::set<unsigned int>		bc_types;
	unsigned int			crumbs_processed;
	struct breadcrumb		*peekbuf;
	std::string			path;
};

class BCrumb
{
public:
	virtual ~BCrumb(void) { Crumbs::freeCrumb(bc); }
	virtual void print(std::ostream& os) const;
	const struct breadcrumb* getBC(void) const { return bc; }
protected:
	BCrumb(struct breadcrumb* in) : bc(in) {}
private:
	struct breadcrumb	*bc;
};

class BCVexReg : public BCrumb
{
public:
	BCVexReg(struct breadcrumb* b) : BCrumb(b) {}
	virtual ~BCVexReg(void) {}
	virtual void print(std::ostream& os) const;
private:
};

class BCErrExit : public BCrumb
{
public:
	BCErrExit(struct breadcrumb* b)
	: BCrumb(b)
	, msg((const char*)bc_data(b))
	, suff((const char*)bc_data(b) + msg.size()+1) {}
	virtual ~BCErrExit(void) {}
	virtual void print(std::ostream& os) const;
private:
	std::string	msg;
	std::string	suff;
};

class BCSyscall: public BCrumb
{
public:
	BCSyscall(struct breadcrumb* b) : BCrumb(b) {}
	virtual ~BCSyscall(void) {}
	virtual void print(std::ostream& os) const;
	unsigned int getKTestObjs(void) const;
	unsigned int getSysNr(void) const { return getBCS()->bcs_sysnr; }
	unsigned int getXlateSysNr(void) const
	{ return getBCS()->bcs_xlate_sysnr; }
	uint64_t getRet(void) const { return getBCS()->bcs_ret; }
	void consumeOps(KTestStream* kts, Crumbs* c);
private:
	const struct bc_syscall* getBCS(void) const
	{ return (const struct bc_syscall*)getBC(); }
};

class BCSysOp : public BCrumb
{
public:
	BCSysOp(struct breadcrumb* b) : BCrumb(b) {}
	virtual ~BCSysOp(void) {}
	virtual void print(std::ostream& os) const { printSeq(os); }
	void printSeq(std::ostream& os, int seq_num = -1) const;
	unsigned int size(void) const { return getSOP()->sop_sz; }
private:
	const struct bc_sc_memop* getSOP(void) const
	{ return (const struct bc_sc_memop*)getBC(); }

};

class BCMemLog : public BCrumb
{
/* includes base address prior to data + concrete mask */
public:
	BCMemLog(struct breadcrumb* b) : BCrumb(b) {}
	virtual ~BCMemLog() {}
	virtual void print(std::ostream& os) const;
private:
};

class BCStackLog : public BCrumb
{
public:
	BCStackLog(struct breadcrumb* b) : BCrumb(b) {}
	virtual ~BCStackLog() {}
	virtual void print(std::ostream& os) const;
private:
};

}
#endif
