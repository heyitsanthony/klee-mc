#include <stdio.h>
#include <string.h>
#include <iostream>
#include "klee/Expr.h"
#include "PipeFormat.h"

using namespace klee;

const char* PipeSTP::exec_cmd = "stp";
const char* const PipeSTP::sat_args[] = {"stp", "--SMTLIB1", NULL};
const char* const PipeSTP::mod_args[] = {"stp", "--SMTLIB1",  "-p", NULL};

const char* PipeBoolector::exec_cmd = "boolector";
const char* const PipeBoolector::sat_args[] = {"boolector", NULL};
const char* const PipeBoolector::mod_args[] = {"boolector", "-fm", NULL};

const char* PipeZ3::exec_cmd = "z3";
const char* const PipeZ3::sat_args[] = {"z3", "-smt", "-in", NULL};
const char* const PipeZ3::mod_args[] = {"z3", "-smt", "-in", "-m", NULL};

const char* PipeBoolector15::exec_cmd = "boolector-1.5";
const char* const PipeBoolector15::sat_args[] =
{"boolector-1.5", "--smt1", NULL};
const char* const PipeBoolector15::mod_args[] =
{"boolector-1.5", "--smt1", "-fm", NULL};


void PipeFormat::readArray(
	const Array* a,
	std::vector<unsigned char>& ret) const
{
	PipeArrayMap::const_iterator	it(arrays.find(a->name));
	unsigned char			default_val;

	if (it == arrays.end()) {
		/* may want values of arrays that aren't present;
		 * this is ok-- give 0's */
		ret.clear();
	} else {
		ret = it->second;
	}

	default_val = (*(defaults.find(a->name))).second;
	ret.resize(a->mallocKey.size, default_val);
}

bool PipeFormat::parseSAT(std::istream& is)
{
	std::string	s;
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

bool PipeSTP::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();
	defaults.clear();

	while (is.getline(line, 512)) {
		char		*cur_buf = line;
		char		arrname[128];
		size_t		sz;
		uint64_t	off, val;

		if (memcmp("ASSERT( ", line, 8) != 0)
			break;
		cur_buf += 8;
		// ASSERT( const_arr1[0x00000054] = 0x00 );
		// Probably the most complicated sscanf I've ever written.
		sz = sscanf(cur_buf, "%[^[][0x%lx] = 0x%lx );", arrname, &off, &val);
		assert (sz == 3);
		addArrayByte(arrname, off, (unsigned char)val);
	}

	return parseSAT(line);
}

static uint64_t bitstr2val(const char* bs)
{
	uint64_t ret = 0;
	for (unsigned int i = 0; bs[i]; i++) {
		ret <<= 1;
		ret |= (bs[i] == '1') ? 1 : 0;
	}
	return ret;
}

bool PipeBoolector::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();
	defaults.clear();

	is.getline(line, 512);

	if (!parseSAT(line)) return false;
	if (!is_sat) return true;

	// const_arr1[00000000000000000000000000000110] 00000100
	while (is.getline(line, 512)) {
		char		arrname[128];
		char		idx_bits[128];
		char		val_bits[128];
		size_t		sz;
		uint64_t	idx, val;

		sz = sscanf(line, "%[^[][%[01]] %[01]",
			arrname, idx_bits, val_bits);
		assert (sz == 3);

		idx = bitstr2val(idx_bits);
		val = bitstr2val(val_bits);
		addArrayByte(arrname, idx, (unsigned char)val);
	}

	return true;
}

bool PipeZ3::parseModel(std::istream& is)
{
	char				line[512];
	std::map<int, std::string>	idx2name;

	arrays.clear();
	defaults.clear();


	//const_arr1 -> as-array[k!0]
	//k!0 -> {
	//  bv10[32] -> bv4[8]
	//  else -> bv4[8]
	//}
	//k!1 -> {
	//  bv2[32] -> bv100[8]
	//}
	//sat
	while (is.getline(line, 512)) {
		char	arrname[128];
		int	arrnum;
		size_t	sz;

		//qemu_buf7 -> as-array[k!1]
		sz = sscanf(line, "%s -> as-array[k!%d]", arrname, &arrnum);
		if (sz == 2) {
			idx2name[arrnum] = arrname;
			continue;
		}

		sz = sscanf(line, "k!%d -> {", &arrnum);
		if (sz == 1) {
			// use arrnum to lookup name
			if (idx2name.count(arrnum) == 0) {
				std::cerr
					<< "Unexpected array index "
					<< arrnum
					<< std::endl;
				return false;
			}

			if (!readArrayValues(is, idx2name[arrnum]))
				return false;
			continue;
		}

		/* 'sat' expected */
		break;
	}

	return parseSAT(line);
}

bool PipeZ3::readArrayValues(std::istream& is, const std::string& arrname)
{
	char		line[512];
	std::set<int>	used_idx;
	unsigned int	max_idx = 0;
	unsigned char	cur_default = 0;

	while (is.getline(line, 512)) {
		size_t		sz;
		unsigned int	idx, val;

		if (line[0] == '}')
			break;

		sz = sscanf(line, "  bv%u[32] -> bv%u[8]", &idx, &val);
		if (sz == 2) {
			used_idx.insert(idx);
			if (max_idx < idx) max_idx = idx;
			addArrayByte(arrname.c_str(), idx, val);
			continue;
		}

		sz = sscanf(line, "  else -> bv%u[8]", &val);
		if (sz == 1) {
			defaults[arrname] = (unsigned char)val;
			cur_default = val;
			continue;
		}

		return false;
	}

	/* patch up 0's */
	for (unsigned int i = 0; i < max_idx; i++) {
		if (used_idx.count(i))
			continue;
		addArrayByte(arrname.c_str(), i, cur_default);
	}

	return true;
}
