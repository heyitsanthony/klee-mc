#ifndef PIPEFORMAT_H
#define PIPEFORMAT_H

#include <map>
#include <vector>
#include <string>

namespace klee
{
class Array;

struct PipeArray
{
	PipeArray(unsigned char def = 0x00) : default_v(def) {}
	unsigned char			default_v;
	std::map<unsigned /* idx */, unsigned char> dat;
};


typedef std::map<std::string, PipeArray> PipeArrayMap;

class PipeFormat
{

public:
	PipeFormat(const char* in_name)
	: is_sat(false), name(in_name)
	{}

	virtual ~PipeFormat() {}

	virtual const char* getExec(void) const = 0;
	virtual const char** getArgvModel(void) const = 0;
	virtual const char** getArgvSAT(void) const = 0;

	virtual bool parseSAT(std::istream& is);
	virtual bool parseModel(std::istream& is) = 0;
	void readArray(const Array* a, std::vector<unsigned char>& ret) const;
	bool isSAT(void) const { return is_sat; }
	const char* getName(void) const { return name; }
protected:
	void setName(const char* in_name) { name = in_name; }
	bool parseSAT(const char* s);
	void addArrayByte(const char* arrNamme, unsigned int off, unsigned char v);
	void setDefaultByte(const char* arrName, unsigned char v);
	bool		is_sat;
	PipeArrayMap		arrays;
private:
	const char*	name;
};

#define DECL_PUB_PIPE_FMT(x)	\
public:	\
	Pipe##x(void) : PipeFormat(#x) {}	\
	virtual ~Pipe##x() {}	\
	virtual const char* getExec(void) const { return exec_cmd; } \
	virtual const char** getArgvSAT(void) const { return sat_args; } \
	virtual const char** getArgvModel(void) const { return mod_args; } \
	virtual bool parseModel(std::istream& is);\
private:	\
	static const char* exec_cmd;	\
	static const char* sat_args[];	\
	static const char* mod_args[];


class PipeSTP : public PipeFormat {
DECL_PUB_PIPE_FMT(STP)
};

class PipeBoolector : public PipeFormat {
DECL_PUB_PIPE_FMT(Boolector)
};

class PipeZ3 : public PipeFormat {
DECL_PUB_PIPE_FMT(Z3)
	bool readArrayValues(std::istream& is, const std::string& arrname);
};

class PipeCVC3 : public PipeFormat { DECL_PUB_PIPE_FMT(CVC3) };
class PipeCVC4 : public PipeFormat { DECL_PUB_PIPE_FMT(CVC4) };

class PipeBoolector15 : public PipeBoolector
{
public:
	PipeBoolector15(void)  { setName("Boolector15"); }
	virtual ~PipeBoolector15() {}
	virtual const char* getExec(void) const { return exec_cmd; }
	virtual const char** getArgvSAT(void) const { return sat_args; }
	virtual const char** getArgvModel(void) const { return mod_args; }
private:
	static const char* exec_cmd;
	static const char* sat_args[];
	static const char* mod_args[];
};

class PipeYices : public PipeFormat
{
DECL_PUB_PIPE_FMT(Yices)
};

class PipeYices2 : public PipeFormat
{
DECL_PUB_PIPE_FMT(Yices2)
};

}

#endif
