/**
 * Dumps crumb files in a human readable format
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "klee/breadcrumb.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/Crumbs.h"

using namespace klee;

static void processCrumbs(KTestStream* kts, Crumbs* crumbs)
{
	BCrumb		*bc;
	unsigned int	crumb_c = 0;

	while ((bc = crumbs->nextBC()) != NULL) {
		BCSyscall	*bcs;

		printf("<breadcrumb seq=\"%d\">\n", crumb_c++);

		bcs = dynamic_cast<BCSyscall*>(bc);
		if (bcs != NULL) {
			bcs->print(std::cout);
			bcs->consumeOps(kts, crumbs);
		} else {
			bc->print(std::cout);
		}
		delete bc;

		printf("</breadcrumb>\n\n");
	}
}

int main(int argc, char* argv[])
{
	Crumbs		*crumbs = NULL;
	KTestStream	*kts = NULL;
	int		ret = -1;
	unsigned int	test_num;
	const char	*dirname;
	char		fname_ktest[256];
	char		fname_crumbs[256];

	if (argc < 2) {
		fprintf(stderr, "Usage: %s testnum [directory]\n", argv[0]);
		goto done;
	}

	test_num = atoi(argv[1]);
	fprintf(stderr, "Replay: test #%d\n", test_num);

	dirname = (argc == 3) ? argv[2] : "klee-last";
	snprintf(fname_ktest, 256, "%s/test%06d.ktest.gz", dirname, test_num);
	snprintf(fname_crumbs, 256, "%s/test%06d.crumbs.gz", dirname, test_num);

	kts = KTestStream::create(fname_ktest);
	if (kts == NULL) {
		fprintf(stderr, "No ktest file at %s\n", fname_ktest);
		goto done;
	}

	/* ignore argv objects, if any */
	if (kts->getKTest()->symArgvs) {
		do {
			const KTestObject	*kto;

			kto = kts->peekObject();
			if (kto == NULL || strcmp(kto->name, "argv"))
				break;
			kts->nextObject();
		} while (1);
	}


	crumbs = Crumbs::create(fname_crumbs);
	if (crumbs == NULL) {
		fprintf(stderr, "No breadcrumb file at %s\n", fname_crumbs);
		goto done;
	}

	printf("<breadcrumbs>\n");
	processCrumbs(kts, crumbs);
	printf("</breadcrumbs>\n");

	ret = 0;
done:
	if (kts) delete kts;
	if (crumbs) delete crumbs;

	return ret;
}