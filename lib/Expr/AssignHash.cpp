#include "AssignHash.h"

using namespace klee;

static const uint8_t samp_seq_dat[] = {0xfe, 0, 0x7f, 1, 0x27, 0x2, ~0x27};
static const uint8_t samp_seq_onoff_dat[] = {0xff, 0x0};

static std::vector<uint8_t>	sample_seq;
static std::vector<uint8_t>	samp_seq_onoff;


/* 64-bit machine = 8 byte sampling => 17 = nyquist rate */
#define NONSEQ_COUNT	17
static std::vector<uint8_t>	sample_nonseq_zeros[NONSEQ_COUNT];
static std::vector<uint8_t>	sample_nonseq_fe[NONSEQ_COUNT];

static void initSamples(void)
{
	sample_seq = std::vector<uint8_t>(&samp_seq_dat[0], &samp_seq_dat[7]);
	samp_seq_onoff = std::vector<uint8_t>(
		&samp_seq_onoff_dat[0], &samp_seq_onoff_dat[2]);

	for (unsigned i = 0; i < NONSEQ_COUNT; i++) {
		for (unsigned j = 0; j < i; j++) {
			sample_nonseq_zeros[i].push_back(0xa5);
		}
		sample_nonseq_zeros[i].push_back(0);
	}

	for (unsigned i = 0; i < NONSEQ_COUNT; i++) {
		for (unsigned j = 0; j < i; j++) {
			sample_nonseq_fe[i].push_back(0);
		}
		sample_nonseq_fe[i].push_back(0xfe);
	}
}

#define BINDSEQ_W(T,W)	\
for (int k = -255; i <= 255; k++) {	\
	T		x = k;			\
	unsigned char*	p = (unsigned char*)&x;	\
	std::vector<unsigned char> seq;	\
	for (int i = 0; i < W; i++) seq.push_back(p[i]);	\
	a.bindFreeToSequence(seq);		\
	ah.commitAssignment();			\
}

uint64_t AssignHash::getEvalHash(const ref<Expr>& e, bool& maybeConst)
{
	AssignHash		ah(e);
	Assignment		&a(ah.getAssignment());

	/* sample vectors initialized? */
	if (sample_seq.empty())
		initSamples();

	for (unsigned k = 0; k < NONSEQ_COUNT; k++) {
		a.bindFreeToSequence(sample_nonseq_zeros[k]);
		ah.commitAssignment();
	}

	for (unsigned k = 0; k < NONSEQ_COUNT; k++) {
		a.bindFreeToSequence(sample_nonseq_fe[k]);
		ah.commitAssignment();
	}

	a.bindFreeToSequence(sample_seq);
	ah.commitAssignment();

	a.bindFreeToSequence(samp_seq_onoff);
	ah.commitAssignment();

	for (unsigned k = 0; k <= 255; k++) {
		a.bindFreeToU8((uint8_t)k);
		ah.commitAssignment();
	}

#ifdef SAMPLE_HARDER
	BINDSEQ_W(int16_t, 2)
	BINDSEQ_W(int32_t, 3)
	BINDSEQ_W(int32_t, 4)
	BINDSEQ_W(int64_t, 8)
#endif

	maybeConst = ah.maybeConst();
	return ah.getHash();
}


