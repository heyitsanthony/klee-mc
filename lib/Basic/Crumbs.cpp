/* XXX they took out the sweet macros */
#if 0
static const char* sysname_tab[] =
{
//#define __SYSCALL(a,b) [a] = #b,
#define __SYSCALL(a,b) #b ,
#include <asm/unistd.h>
};
#endif
#define SYSNAME_MAX	sizeof(sysname_tab)/sizeof(const char*)

#include <klee/breadcrumb.h>
#include <klee/Internal/ADT/Crumbs.h>
#include <klee/Internal/ADT/zfstream.h>
#include <klee/Internal/ADT/KTestStream.h>
#include <unistd.h>
#include <string>
#include <string.h>
#include <assert.h>
#include <fstream>

using namespace klee;

Crumbs* Crumbs::create(const char* fname)
{
	Crumbs	*ret = new Crumbs(fname);
	if (ret->is == NULL) {
		delete ret;
		ret = NULL;
	}

	return ret;
}

Crumbs::Crumbs(const char* fname)
: is(NULL)
, crumbs_processed(0)
, peekbuf(NULL)
{
	/* XXX: load on demand */
	is = getFile(fname);
	if (is == NULL)
		return;

	loadTypeList(*is);
	delete is;

	is = getFile(fname);
	assert (is != NULL);
}

std::istream* Crumbs::getFile(const char* fname)
{
	std::istream	*ret;
	const char	*gzSuffix;

	gzSuffix = strstr(fname, ".gz");
	if (gzSuffix && strlen(gzSuffix) == 3) {
		ret = new gzifstream(fname, std::ios::binary | std::ios::in);
	} else {
		ret = new std::ifstream(fname, std::ios::binary | std::ios::in);
	}

	if (ret == NULL)
		return NULL;

	if (!ret->good()) {
		delete ret;
		return NULL;
	}

	return ret;
}


Crumbs::~Crumbs()
{
	if (peekbuf != NULL) freeCrumb(peekbuf);
	if (is != NULL) delete is;
}

void Crumbs::skip(unsigned int i)
{
	while (i) {
		struct breadcrumb* bc = next();
		if (!bc) break;
		freeCrumb(bc);
		i--;
	}
}

struct breadcrumb* Crumbs::peek(void)
{
	char			*ret;

	if (peekbuf == NULL)
		peekbuf = next();

	if (peekbuf == NULL)
		return NULL;

	ret = new char[peekbuf->bc_sz];
	memcpy(ret, peekbuf, peekbuf->bc_sz);
	return reinterpret_cast<struct breadcrumb*>(ret);
}

struct breadcrumb* Crumbs::next(void)
{
	struct breadcrumb	hdr;
	char			*ret;

	if (peekbuf != NULL) {
		struct breadcrumb *tmp = peekbuf;
		peekbuf = NULL;
		return tmp;
	}

	if (is->eof() || !is->good())
		return NULL;

	if (!is->read((char*)&hdr, sizeof(hdr)))
		return NULL;

	ret = new char[hdr.bc_sz];
	memcpy(ret, &hdr, sizeof(hdr));

	if (!is->read((char*)(ret + sizeof(hdr)), hdr.bc_sz - sizeof(hdr)))
		return NULL;

	crumbs_processed++;
	return reinterpret_cast<struct breadcrumb*>(ret);
}

void Crumbs::freeCrumb(struct breadcrumb* bc)
{ delete [] reinterpret_cast<char*>(bc); }

struct breadcrumb* Crumbs::next(unsigned int bc_type)
{
	struct breadcrumb	*bc;
	while ((bc = next()) != NULL) {
		if (bc->bc_type == bc_type)
			return bc;
		Crumbs::freeCrumb(bc);
	}
	return bc;
}

bool Crumbs::hasType(unsigned int v) const
{ return (bc_types.count(v) != 0); }

void Crumbs::loadTypeList(std::istream& cur_is)
{
	struct breadcrumb	*cur_bc;
	unsigned int		old_processed;

	assert (bc_types.size() == 0);

	old_processed = crumbs_processed;
	while ((cur_bc = next()) != NULL) {
		bc_types.insert(cur_bc->bc_type);
		freeCrumb(cur_bc);
	}

	crumbs_processed = old_processed;
}

void BCrumb::print(std::ostream& os) const { os << "BCrumb" << std::endl; }


BCrumb* Crumbs::toBC(struct breadcrumb* bc)
{
	if (bc == NULL) return NULL;
	switch (bc->bc_type) {
	case BC_TYPE_SC:
		return new BCSyscall(bc);
	case BC_TYPE_VEXREG:
		return new BCVexReg(bc);
	case BC_TYPE_SCOP:
		return new BCSysOp(bc);
//	case BC_TYPE_BOGUS:
	default:
		assert (0 == 1);
	}
	return NULL;
}

void BCVexReg::print(std::ostream& os) const { os << "VEXREG\n"; }

/* Meant to consume a system call op.
 * System calls have a sequence of associated sysops, which
 * pull in data to addreses, etc.
 * This eats them all.
 * */
void BCSyscall::consumeOps(KTestStream* kts, Crumbs* crumbs)
{
	/* read new register */
	if (bc_sc_is_newregs(getBCS())) {
		unsigned	br;
		const KTestObject* kto = kts->nextObject();
		assert (kto);
		br = kto->numBytes;
		if (br != 921 && br != 361) {
			fprintf(stderr, "GOT %d BYTES. WHOOPS!\n", kto->numBytes);
		}
		assert (br == 921|| br == 361);
	}

	/* read in any objects written out */
	/* go through all attached ops specifically */
	for (unsigned int i = 0; i < getBCS()->bcs_op_c; i++) {
		const KTestObject	*kto;
		BCrumb			*bcr;
		BCSysOp			*sop;

		bcr = crumbs->nextBC();
		assert (bcr);
		sop = dynamic_cast<BCSysOp*>(bcr);
		assert (sop);

		sop->printSeq(std::cout, i);

		kto = kts->nextObject();
		assert (kto);

		if (sop->size() != kto->numBytes) {
			std::cerr << "Size mismatch on " <<
				kto->name << ": crumb=" <<
				sop->size() << " vs " <<
				kto->numBytes << " =ktest\n";
			std::cerr << "Failed syscall="
				<< getSysNr() << std::endl;

		}
		assert (sop->size() == kto->numBytes);

		delete bcr;
	}
}

unsigned int BCSyscall::getKTestObjs(void) const
{
	return getBCS()->bcs_op_c +
		((bc_sc_is_newregs(getBCS())) ? 1 : 0);
}

const char* get_sysnr_str(unsigned int n)
{
//	if (n < SYSNAME_MAX)
//		return sysname_tab[n];
	return NULL;
}

void BCSyscall::print(std::ostream& os) const
{
	const char* sysnr_name;

	os << "<syscall>\n";
	if ((sysnr_name = get_sysnr_str(getSysNr())) != NULL)
		os << "<name>" << sysnr_name << "</name>\n";
	os << "<nr>" << getSysNr() << "</nr>\n";

	os << "<xlate_nr>" << getXlateSysNr() << "</xlate_nr>\n";

	os << "<flags>" << (void*)(long)getBC()->bc_type_flags << "</flags>\n";
	os << "<ret>" << (void*)getRet() << "</ret>\n";
	os << "<opC>" << (int)getBCS()->bcs_op_c << "</opC>\n";
	os << "<testObjs>" << getKTestObjs() << "</testObjs>\n";
	os << "</syscall>\n";
}

void BCSysOp::printSeq(std::ostream& os, int seq_num) const
{
	/* print seq number if one given */
	if (seq_num == -1)
		os << "<sysop>\n";
	else
		os << "<sysop seq=\"" << seq_num << "\">\n";

	os << "<size>" <<  getSOP()->sop_sz << "</size>\n";
	os << "<base>" << getSOP()->sop_baseptr.ptr << "</base>\n";
	os << "<offset>" << getSOP()->sop_off << "</offset>\n";

	os << "</sysop>\n";
}
