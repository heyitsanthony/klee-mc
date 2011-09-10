#ifndef PIPEFORMAT_H
#define PIPEFORMAT_H

#include <map>
#include <vector>
#include <string>

namespace klee
{
class Array;


typedef std::map<std::string, std::vector<unsigned char> > PipeArrayMap;
typedef std::map<std::string, unsigned char > PipeArrayDefaults;

class PipeFormat
{

public:
	PipeFormat(const char* in_name)
	: is_sat(false), name(in_name)
	{}

	virtual ~PipeFormat() {}

	virtual const char* getExec(void) const = 0;
	virtual const char* const* getArgvModel(void) const = 0;
	virtual const char* const* getArgvSAT(void) const = 0;

	virtual bool parseSAT(std::istream& is);
	virtual bool parseModel(std::istream& is) = 0;
	void readArray(const Array* a, std::vector<unsigned char>& ret) const;
	bool isSAT(void) const { return is_sat; }
	const char* getName(void) const { return name; }
protected:
	bool parseSAT(const char* s);
	void addArrayByte(const char* arrNamme, unsigned int off, unsigned char v);
	bool		is_sat;
	PipeArrayMap		arrays;
	PipeArrayDefaults	defaults;
private:
	const char*	name;
};

#define DECL_PUB_PIPE_FMT(x)	\
public:	\
	Pipe##x(void) : PipeFormat(#x) {}	\
	virtual ~Pipe##x() {}	\
	virtual const char* getExec(void) const { return exec_cmd; } \
	virtual const char* const* getArgvSAT(void) const { return sat_args; } \
	virtual const char* const* getArgvModel(void) const { return mod_args; } \
	virtual bool parseModel(std::istream& is);

class PipeSTP : public PipeFormat {
DECL_PUB_PIPE_FMT(STP)
private:
	static const char* exec_cmd;
	static const char* const sat_args[];
	static const char* const mod_args[];
};


class PipeBoolector : public PipeFormat {
DECL_PUB_PIPE_FMT(Boolector)
private:
	static const char* exec_cmd;
	static const char* const sat_args[];
	static const char* const mod_args[];
};

class PipeZ3 : public PipeFormat {
DECL_PUB_PIPE_FMT(Z3)
private:
	bool readArrayValues(std::istream& is, const std::string& arrname);
	static const char* exec_cmd;
	static const char* const sat_args[];
	static const char* const mod_args[];
};
}

#endif
