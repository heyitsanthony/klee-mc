#include <stdio.h>
#include <string.h>
#include <iostream>
#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/Expr.h"
#include "PipeFormat.h"

using namespace klee;

//#define USE_IOMON
#ifdef USE_IOMON
#define IOMON_EXEC(x) "stdsteal"
#define IOMON_ARGS(x) "stdsteal", x
#else
#define IOMON_ARGS(x) x
#define IOMON_EXEC(x) x
#endif
#define DECL_ARGS(x) const char* const x[] =


const char* PipeSTP::exec_cmd = IOMON_EXEC("stp");
DECL_ARGS(PipeSTP::sat_args) {IOMON_ARGS("stp"), "--SMTLIB1", NULL};
DECL_ARGS(PipeSTP::mod_args) {IOMON_ARGS("stp"), "--SMTLIB1",  "-p", NULL};

const char* PipeBoolector::exec_cmd = IOMON_EXEC("boolector");
DECL_ARGS(PipeBoolector::sat_args) {IOMON_ARGS("boolector"), NULL};
DECL_ARGS(PipeBoolector::mod_args) {IOMON_ARGS("boolector"), "-fm", NULL};

const char* PipeZ3::exec_cmd = IOMON_EXEC("z3");
DECL_ARGS(PipeZ3::sat_args) {IOMON_ARGS("z3"), "-smt", "-in", NULL};
DECL_ARGS(PipeZ3::mod_args) {IOMON_ARGS("z3"), "-smt", "-in", "-vldt", NULL};

const char* PipeBoolector15::exec_cmd = IOMON_EXEC("boolector-1.5");
DECL_ARGS(PipeBoolector15::sat_args) 
	{IOMON_ARGS("boolector-1.5"), "--smt1", NULL};
DECL_ARGS(PipeBoolector15::mod_args)
	{IOMON_ARGS("boolector-1.5"), "--smt1", "-fm", NULL};

const char* PipeCVC3::exec_cmd = IOMON_EXEC("cvc3");
DECL_ARGS(PipeCVC3::sat_args) {	IOMON_ARGS("cvc3"), "-lang", "smt", NULL};
DECL_ARGS(PipeCVC3::mod_args) { IOMON_ARGS("cvc3"),
	"-lang", "smt", "-output-lang", "presentation", "+model", NULL};

/* XXX BUSTED-- how do I get model data from cmdline? */
const char* PipeCVC4::exec_cmd = "cvc4";
DECL_ARGS(PipeCVC4::sat_args) { IOMON_ARGS("cvc4"), "-lang", "smt1", NULL};
DECL_ARGS(PipeCVC4::mod_args) { IOMON_ARGS("cvc4"),
	"-lang", "smt1", "-output-lang", "presentation", "+model", NULL};

const char* PipeYices::exec_cmd = IOMON_EXEC("yices");
DECL_ARGS(PipeYices::sat_args) {IOMON_ARGS("yices"),  NULL};
DECL_ARGS(PipeYices::mod_args) {IOMON_ARGS("yices"), "-f", NULL};

const char* PipeYices2::exec_cmd = IOMON_EXEC("yices_smt");
DECL_ARGS(PipeYices2::sat_args) {IOMON_ARGS("yices_smt"), NULL};
DECL_ARGS(PipeYices2::mod_args) {IOMON_ARGS("yices_smt"), "--show-model", NULL};

void PipeFormat::readArray(
	const Array* a,
	std::vector<unsigned char>& ret) const
{
	PipeArrayMap::const_iterator	it(arrays.find(a->name));
	unsigned char			default_val = 0;

	assert (a->mallocKey.size < 0x10000000 && "Array too large");

	ret.clear();
	if (it == arrays.end()) {
		ret.resize(a->mallocKey.size, default_val);
	} else {
		default_val = it->second.default_v;
		ret.resize(a->mallocKey.size, default_val);
		foreach (it2, it->second.dat.begin(), it->second.dat.end()) {
			if (it2->first >= a->mallocKey.size) {
				std::cerr << "[PipeFormat] Warning. idx:"
					<< it2->first << " >= " << "arr_sz: "
					<< a->mallocKey.size << ". Ignoring.\n";
				continue;
			}
			ret[it2->first] = it2->second;
		}
	}
}

void PipeFormat::setDefaultByte(const char* arrName, unsigned char v)
{
	auto it(arrays.find(arrName));
	if (it == arrays.end()) {
		PipeArray	pa(v);
		arrays[arrName] = pa;
	} else {
		arrays[arrName].default_v = v;
	}
}

void PipeFormat::addArrayByte(
	const char* arrName, unsigned int off, unsigned char v)
{
	if (off > 0x10000000) {
		klee_warning_once(0, "PipeFormat: Huge Index. Buggy Model?");
		return;
	}

	auto it(arrays.find(arrName));
	if (it == arrays.end()) {
		PipeArray	pa;
		pa.dat[off] = v;
		arrays[arrName] = pa;
	} else {
		assert (!it->second.dat.count(off) && "Double-assign array?");
		it->second.dat[off] = v;
	}
}

bool PipeSTP::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();

	memset(line, 0, 8);
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

static uint64_t bitstr2val(const char* bs)
{
	uint64_t ret = 0;
	for (unsigned int i = 0; bs[i]; i++) {
		ret <<= 1;
		ret |= (bs[i] == '1') ? 1 : 0;
	}
	return ret;
}

bool PipeYices::parseModel(std::istream& is)
{
	char		line[512];
	char		cur_arrname[128];

	arrays.clear();

	if (!parseSAT(is)) return false;
	if (!isSAT()) return true;

	is.getline(line, 512);	/* blank line */
	is.getline(line, 512);	/* blank, again */
	is.getline(line, 512);	/* MODEL */
	if (strcmp(line, "MODEL") != 0) return false;

/*
	sat

	MODEL
	--- const_arr1 ---
	(= (const_arr1 0b00000000000000000000000000001111) 0b00000100)
	default: 0b00101110
	--- qemu_buf7 ---
	(= (qemu_buf7 0b00000000000000000000000000001111) 0b00101110)
	default: 0b01100100
	----
*/
	cur_arrname[0] = '\0';
	while (is.getline(line, 512)) {
		char		in_arrname[128];
		size_t		sz;
		char		off_bitstr[128], val_bitstr[128];
		uint64_t	off, val;

		sz = sscanf(line, "--- %s ---", in_arrname);
		if (sz == 1) {
			strcpy(cur_arrname, in_arrname);
			continue;
		}

		sz = sscanf(line, "(= (%s 0b%[01]) 0b%[01])",
			in_arrname, off_bitstr, val_bitstr);
		if (sz == 3) {
			off = bitstr2val(off_bitstr);
			val = bitstr2val(val_bitstr);
			addArrayByte(cur_arrname, off, (unsigned char)val);
			continue;
		}

		sz = sscanf(line, "default: 0b%[01]", val_bitstr);
		if (sz == 1) {
			setDefaultByte(cur_arrname, bitstr2val(val_bitstr));
			continue;
		}
	}

	return true;
}


bool PipeCVC4::parseModel(std::istream& is) { assert (0 == 1 && "UGH"); }

bool PipeCVC3::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();

	is.getline(line, 512);
	if (strcmp(line, "Satisfiable.") != 0) {
		std::cerr << "Not a SAT model??\n";
		return false;
	}
	is_sat = true;

	while (is.getline(line, 511)) {
		char		arrname[128];
		char		off_bitstr[128], val_bitstr[128];
		size_t		sz;
		uint64_t	off, val;

		if (strlen(line) > 256)
			continue;
		// XXX
		// ARGH. IT HAS A FULL GRAMMAR ALA
		// ASSERT (reg4 = (ARRAY (arr_var: BITVECTOR(32)): 0bin01111111));
		// OH WELL.
		sz = sscanf(line, "ASSERT (%[^[][0bin%[01]] = 0bin%[01]);",
			arrname, off_bitstr, val_bitstr);
		if (sz != 3) {
			continue;
		}

		off = bitstr2val(off_bitstr);
		val = bitstr2val(val_bitstr);
		addArrayByte(arrname, off, (unsigned char)val);
	}

	return true;
}

bool PipeBoolector::parseModel(std::istream& is)
{
	char	line[512];

	arrays.clear();

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
		int	default_val;
		int	arrnum;
		size_t	sz;

		// An array declaration,
		//qemu_buf7 -> as-array[k!1]
		sz = sscanf(line, "%s -> as-array[k!%d]", arrname, &arrnum);
		if (sz == 2) {
			idx2name[arrnum] = arrname;
			continue;
		}

		/* beginning of an array model */
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

		/* constant array */
		sz = sscanf(line, "%s -> Array!val!%d", arrname, &default_val);
		if (sz == 2) {
			setDefaultByte(arrname, (unsigned char)default_val);
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

	while (is.getline(line, 512)) {
		size_t		sz;
		unsigned int	idx, val;

		if (line[0] == '}')
			break;

		sz = sscanf(line, "  bv%u[32] -> bv%u[8]", &idx, &val);
		if (sz == 2) {
			addArrayByte(arrname.c_str(), idx, val);
			continue;
		}

		sz = sscanf(line, "  else -> bv%u[8]", &val);
		if (sz == 1) {
			setDefaultByte(arrname.c_str(), (unsigned char)val);
			continue;
		}

		return false;
	}

	return true;
}


bool PipeYices2::parseModel(std::istream& is)
{
	char		line[512];
	bool		got_sat;

	arrays.clear();

	got_sat = parseSAT(is);
	if (!got_sat) {
		return false;
	}

	/* 
	 * FORMAT:
	 * sat
	 *
	 * MODEL
	 * (function readbuf4
	 *  (type (-> (bitvector 32) (bitvector 8)))
	 *  (= (readbuf4 0b00000000000000000000000000000111) 0b10000000)
	 *  (default 0b00000000))
	 *  ----
	 */
	line[0] = '\0';
	if (!(is >> line)) goto oops;
	if (strcmp(line, "MODEL")) goto oops;
	if (!is.getline(line, 512)) goto oops;

	while (1) {
		char arrname[128];
		if (!is.getline(line, 512)) goto oops;

		if (sscanf(line, "(function %s", arrname) != 1) {
			if (strcmp("----", line))
				goto oops;
			break;
		}
		if (!is.getline(line, 512)) goto oops;
		if (strcmp(" (type (-> (bitvector 32) (bitvector 8)))", line))
			goto oops;
		while (1) {
			char arrname2[128], off_bitstr[64], val_bitstr[64];
			if (!is.getline(line, 512))
				goto oops;
			if (sscanf(line, " (= (%s 0b%[01]) 0b%[01])",
				arrname2, off_bitstr, val_bitstr) == 3)
			{
				uint32_t off = bitstr2val(off_bitstr);
				uint8_t val = bitstr2val(val_bitstr);
				addArrayByte(arrname, off, val);
				continue;
			}
			if (!sscanf(line, " (default 0b%[01]))", val_bitstr))
				goto oops;
			setDefaultByte(arrname, bitstr2val(val_bitstr));
			break;
		}
	}

	return true;
oops:
	std::cerr << "[yices2] BAD LINE '" << line << "'\n";
	return false;
}

