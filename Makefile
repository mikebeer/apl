# Word-glyph APL interpreter, portable C89.
CC      ?= cc
CFLAGS  ?= -O2 -std=c89 -pedantic -Wall -Wextra
LIBS    = -lm

all: apl

apl: apl_all.c
	$(CC) $(CFLAGS) -o $@ apl_all.c $(LIBS)

# the single-file form is generated from src/ (this is what to ship to
# platforms that want one source file, e.g. VM/370 CMS)
apl_all.c: src/apl.c src/p0_base.c src/p1_val.c src/p2_lex.c src/p3_prim.c src/p3b_ap.c \
           src/p4_eval.c src/p4b_oo.c src/p5_sys.c src/p5b_oosys.c tools/amalg.sh
	sh tools/amalg.sh > apl_all.c

test: apl
	APL=./apl sh tests/run.sh

clean:
	rm -f apl apl_all.c

.PHONY: all test clean
