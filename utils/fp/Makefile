CLASSES=	fp-float-float-float		\
		fp-double-double-double		\
		fp-double-float-float		\
		fp-double-float-double		\
		fpcmp-float-float		\
		fpcmp-double-double		\
		fpcmp-double-float		\
		fptri-float-float-float		\
		fptri-double-double-double		\
		fptri-double-float-float		\
		fptri-double-float-double		\


BINPATHS=$(CLASSES:%=bin/%)

all: $(BINPATHS)

clean:
	rm -rf $(BINPATHS) fpguests*


guests: fpguests.tar.gz

fpguests:
	./make_guests.sh

fpguests.tar.gz: $(BINPATHS)
	./make_guests.sh
	tar cvf fpguests.tar fpguests
	gzip fpguests.tar


bin/fp-simple: fp-simple.c
	gcc fp-simple.c -o $@

bin/fp-simple2: fp-simple2.c
	gcc fp-simple2.c -o $@


bin/fp-%:
	gcc -o $@-add	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=+				\
	fp.c
	gcc -o $@-sub	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=-				\
	fp.c
	gcc -o $@-mul	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=\*				\
	fp.c
	gcc -o $@-div	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=/				\
	fp.c

bin/fptri-%:
	gcc -o $@-add	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=+				\
	fptri.c
	gcc -o $@-sub	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=-				\
	fptri.c
	gcc -o $@-mul	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=\*				\
	fptri.c
	gcc -o $@-div	\
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DTYPE3_OUT=`echo $@ | cut -f4 -d'-'`	\
	-DFPOP=/				\
	fptri.c

bin/fpcmp-%:
	gcc -o $@-lt \
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DFPOP=\<				\
	fpcmp.c
	gcc -o $@-gt \
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DFPOP=\>				\
	fpcmp.c
	gcc -o $@-eq \
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DFPOP===				\
	fpcmp.c
	gcc -o $@-ne \
	-DTYPE1_IN=`echo $@ | cut -f2 -d'-'`	\
	-DTYPE2_IN=`echo $@ | cut -f3 -d'-'`	\
	-DFPOP=\!=				\
	fpcmp.c