#include <stdio.h>
#include <string.h>
#include "klee/Expr.h"
#include "PipeFormat.h"

using namespace klee;

const char* PipeSTP::exec_cmd = "stp";
const char* PipeBoolector::exec_cmd = "boolector";
const char* PipeZ3::exec_cmd = "z3";

const char* const PipeSTP::sat_args[] = {"stp", "--SMTLIB1", NULL};
const char* const PipeSTP::mod_args[] = {"stp", "--SMTLIB1",  "-p", NULL};
const char* const PipeBoolector::sat_args[] = {"boolector", NULL};
const char* const PipeBoolector::mod_args[] = {"boolector", "-fm", NULL};
const char* const PipeZ3::sat_args[] = {"z3", "-smt", "-in", NULL};
const char* const PipeZ3::mod_args[] = {"z3", "-smt", "-in", "-m", NULL};

#include <iostream>
void PipeFormat::readArray(
	const Array* a,
	std::vector<unsigned char>& ret) const
{
	PipeArrayMap::const_iterator	it(arrays.find(a->name));

	if (it == arrays.end()) {
		/* may want values of arrays that aren't present;
		 * this is ok-- give 0's */
		ret.clear();
	} else {
		ret = it->second;
	}
	ret.resize(a->mallocKey.size, 0);
}

bool PipeFormat::parseSAT(std::istream& is)
{
	std::string	s;
	arrays.clear();
	is >> s;
	return parseSAT(s.c_str());
}

bool PipeFormat::parseSAT(const char* s)
{
	if (strncmp(s, "sat", 3) == 0) {
		is_sat = true;
	} else if (strncmp(s, "unsat", 5) == 0) {
		is_sat = false;
	} else {
		return false;
	}

	return true;
}

bool PipeSTP::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();

	while (is.getline(line, 512)) {
		char		*cur_buf = line;
		char		arrname[128];
		size_t		sz;
		uint64_t	off, val;

		if (memcmp("ASSERT( ", line, 8) != 0)
			break;
		cur_buf += 8;
		// ASSERT( const_arr1_0x1347a5a0[0x00000054] = 0x00 );
		// Probably the most complicated sscanf I've ever written.
		sz = sscanf(cur_buf, "%[^[][0x%lx] = 0x%lx );", arrname, &off, &val);
		assert (sz == 3);
		addArrayByte(arrname, off, (unsigned char)val);
	}

	return parseSAT(line);
}

void PipeFormat::addArrayByte(
	const char* arrName, unsigned int off, unsigned char v)
{
	std::string		arr_s(arrName);
	PipeArrayMap::iterator	it;

	assert (off < 10000000 && "HUGE OFFSET!!");

	it = arrays.find(arrName);
	if (it == arrays.end()) {
		std::vector<unsigned char>	vec(off+1);
		vec[off] = v;
		arrays[arrName] = vec;
		return;
	}

	if (it->second.size() < off+1)
		it->second.resize(off+1);
	it->second[off] = v;
}

bool PipeBoolector::parseModel(std::istream& is)
{
	arrays.clear();
	assert (0 ==1 && "STUB");
}

bool PipeZ3::parseModel(std::istream& is)
{
	arrays.clear();
	assert (0 ==1 && "STUB");
}
