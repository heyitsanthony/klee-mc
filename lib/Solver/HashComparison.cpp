/**
 * Dawson is an asshole who thinks he knows more about the queries showing up
 * than I do. Here is how I use science to justify my observations versus his
 * idiot opinion:
 * 	1. Hash with hash1 (for us, hashes without hasing the array name)
 *	2. Hash with hash2 (hashing *with* the array name)
 * Hence, given arrays a and b.
 * 	h1(* a a) = A
 *	h1(* a b) = A
 * 	h2(* a a) = B
 *	h2(* a b) = C
 *
 * Store every hashpair (h1, h2) to a file. If b != c and (a,b) == (a,c),
 * then we have observed a collision where h1 collides but h2 does not.
 * Hence, h1 is less sound than h2.
 */
#include "HashComparison.h"
#include <fstream>

#include "klee/Constraints.h"
#include "static/Sugar.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace klee;

namespace {
	cl::opt<std::string>
	HashComparisonFile(
		"hash-cmp-file",
		cl::desc("Hash pairs to this file."),
		cl::init(""));
}

#define HASHDUMP_QUERY	\
	if (!in_solver) { in_solver = true; dumpQueryHash(q); in_solver = false; }

HashComparison::HashComparison(Solver* s, QueryHash* h1, QueryHash* h2)
	: SolverImplWrapper(s)
	, in_solver(false)
	, qh1(h1)
	, qh2(h2)
	, of(std::make_unique<std::ofstream>(HashComparisonFile.c_str()))
{
	assert (HashComparisonFile.size());
}

bool HashComparison::computeSat(const Query& q)
{
	HASHDUMP_QUERY
	return doComputeSat(q);
}

Solver::Validity HashComparison::computeValidity(const Query& q)
{
	HASHDUMP_QUERY
	return doComputeValidity(q);
}

ref<Expr> HashComparison::computeValue(const Query& q)
{
	HASHDUMP_QUERY
	return doComputeValue(q);
}

bool HashComparison::computeInitialValues(const Query& q, Assignment& a)
{
	HASHDUMP_QUERY
	return doComputeInitialValues(q, a);
}

void HashComparison::dumpQueryHash(const Query& q)
{
	std::pair<Expr::Hash, Expr::Hash> hashes;

	hashes.first = qh1->hash(q);
	hashes.second = qh2->hash(q);

	if (dumped_pairs.count(hashes))
		return;

	dumped_pairs.insert(hashes);

	of->write((char*)&hashes.first, sizeof(Expr::Hash));
	of->write((char*)&hashes.second, sizeof(Expr::Hash));
	of->flush();
}