#include <klee/breadcrumb.h>
#include <klee/Internal/ADT/Crumbs.h>
#include <klee/Internal/ADT/KTestStream.h>
#include <klee/util/gzip.h>
#include <string>
#include <string.h>
#include <assert.h>

using namespace klee;

Crumbs* Crumbs::create(const char* fname)
{
	Crumbs	*ret = new Crumbs(fname);
	if (ret->f == NULL) {
		delete ret;
		ret = NULL;
	}

	return ret;
}

Crumbs::Crumbs(const char* fname)
: f(NULL)
, crumbs_processed(0)
{
	const char* gzSuffix;

	gzSuffix = strstr(fname, ".gz");
	if (gzSuffix && strlen(gzSuffix) == 3) {
		std::string	dst(
			std::string(fname).substr(0, gzSuffix - fname));

		if (!GZip::gunzipFile(fname, dst.c_str())) {
			fprintf(stderr, "Could not gunzip %s\n", dst.c_str());
			return;
		}

		f = fopen(dst.c_str(), "rb");
		if (f == NULL) return;
	} else {
		f = fopen(fname, "rb");
		if (f == NULL) return;
	}

	/* XXX: load on demand */
	loadTypeList();
}

Crumbs::~Crumbs()
{
	if (f != NULL) fclose(f);
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

struct breadcrumb* Crumbs::peek(void) const
{
	struct breadcrumb	hdr;
	char			*ret;
	size_t			br;
	int			err;

	if (feof(f)) return NULL;

	br = fread(&hdr, sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	ret = new char[hdr.bc_sz];
	memcpy(ret, &hdr, sizeof(hdr));

	br = fread(ret + sizeof(hdr), hdr.bc_sz - sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	err = fseek(f, -((long)hdr.bc_sz), SEEK_CUR);
	assert (err == 0);

	return reinterpret_cast<struct breadcrumb*>(ret);
}

struct breadcrumb* Crumbs::next(void)
{
	struct breadcrumb	hdr;
	char			*ret;
	size_t			br;

	if (feof(f)) return NULL;

	br = fread(&hdr, sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	ret = new char[hdr.bc_sz];
	memcpy(ret, &hdr, sizeof(hdr));

	br = fread(ret + sizeof(hdr), hdr.bc_sz - sizeof(hdr), 1, f);
	if (br != 1) return NULL;

	crumbs_processed++;
	return reinterpret_cast<struct breadcrumb*>(ret);
}

void Crumbs::freeCrumb(struct breadcrumb* bc)
{
	delete [] reinterpret_cast<char*>(bc);
}

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
{
	return (bc_types.count(v) != 0);
}

void Crumbs::loadTypeList(void)
{
	long			old_off;
	struct breadcrumb	*cur_bc;
	unsigned int		old_processed;

	assert (bc_types.size() == 0);

	old_processed = crumbs_processed;
	old_off = ftell(f);
	rewind(f);

	while ((cur_bc = next()) != NULL) {
		bc_types.insert(cur_bc->bc_type);
		freeCrumb(cur_bc);
	}

	fseek(f, old_off, SEEK_SET);
	crumbs_processed = old_processed;
}

void BCrumb::print(std::ostream& os) const
{
	os << "BCrumb" << std::endl;
}


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

void BCVexReg::print(std::ostream& os) const
{
	os << "VEXREG\n";
}

/* Meant to consume a system call op.
 * System calls have a sequence of associated sysops, which
 * pull in data to addreses, etc.
 * This eats them all.
 * */
void BCSyscall::consumeOps(KTestStream* kts, Crumbs* crumbs)
{
	/* read new register */
	if (bc_sc_is_newregs(getBCS())) {
		const KTestObject* kto = kts->nextObject();
		assert (kto);
		if (kto->numBytes != 633) {
			fprintf(stderr, "GOT %d BYTES. WHOOPS!\n", kto->numBytes);
		}
		assert (kto->numBytes == 633);
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

void BCSyscall::print(std::ostream& os) const
{
	os << "<syscall>\n";
	os << "<nr>" << getSysNr() << "</nr>\n";
	os << "<flags>" << (void*)getBC()->bc_type_flags << "</flags>\n";
	os << "<ret>" << (void*)getRet() << "</ret>\n";
	os << "<testObjs>" << getKTestObjs() << "</testObjs>\n";
	os << "</syscall>\n";
}

void BCSysOp::print(std::ostream& os) const
{
	os << "SCOp sz=" << getSOP()->sop_sz << "\n";
}
