# apl - a word-glyph APL interpreter in portable C89

APL2-style interpreter whose glyphs are spelled as lower-case words (`rho`,
`iota`, `reduce plus`, ...). Started as a Regina REXX prototype
(`ref/apl.rex`); this C version has nested arrays (rank up to 8), lambdas,
Dyalog-style control words, user-defined operators (`dop`), `over`/`atop`/
`amend`/`keyed`, `[]SIGNAL` and friends, and the hooks for shared variables
and auxiliary processors. It is meant to run later on VM/370 (CMS).

**The user guide is `docs/APL-User-Guide.docx`.** Every example in it is in
`examples/`.

    make            # builds ./apl from the single-file apl_all.c
    make test       # runs examples and tests/scen against stored output
    ./apl prog.apl  # run a script
    ./apl           # interactive REPL
    sh docs/make_manual.sh   # rebuild the manual (node + docx, soffice, poppler)

## Layout

    src/p0_base.c   types, memory, errors (setjmp/longjmp), character classes
    src/p1_val.c    arrays: refcounts, number formatting, display
    src/p2_lex.c    tokenizer
    src/p3_prim.c   scalar and structural primitives (any rank <= 8)
    src/p3b_ap.c    shared variables / auxiliary processors (hooks + demo processors)
    src/p4_eval.c   variables, user functions, parser/evaluator, operators
    src/p5_sys.c    nabla editor, ) commands, workspaces, REPL, main
    src/apl.c       includes the parts; tools/amalg.sh inlines them into
    apl_all.c       ONE self-contained source file (generated; checked in)

## Portability rules (so it can run on VM/370 CMS and elsewhere)

* Strict C89: no `//`, no declarations after statements, no `long long`,
  no `stdint.h`, no VLAs. Compiles clean with `-std=c89 -pedantic`.
* EBCDIC-safe: character classes use literal sets (`LETTERS`, `UPPER`,
  `LOWER`), never ranges or codepoint arithmetic. Only digits use `'0'..'9'`
  (contiguous in ASCII and EBCDIC).
* Numbers are `double`; array sizes and indices are 32-bit `int`. No NaN or
  infinity is assumed (S/370 uses hexadecimal floating point), division by
  zero is a DOMAIN ERROR (except `0 divide 0`, which is 1).
* All functions are `static`; the only external symbol is `main`, so the
  8-character external-name limit of CMS never bites.
* No platform calls except an optional directory listing for `)LIB`
  (POSIX `dirent`, enabled by `APL_HAVE_DIRENT`; other platforms print a
  notice). Array size is capped by `APL_MAXELEMS` (default 16M elements).
* Statement-level errors use `setjmp`/`longjmp`; on `__CMS__` the two are
  declared explicitly because GCCLIB31's `setjmp.h` only has the `jmp_buf`
  typedef.

Verified: gcc and clang, `-std=c89 -pedantic` through C11; x86-64, 32-bit
(`-m32`), big-endian s390x (qemu); `-funsigned-char`; ASan/UBSan clean on
the tests and on 40,000 random fuzz lines; compiles without warnings
against the VM/370plus GCCLIB31 headers (`s390x-linux-gnu-gcc -m31 ... -I
gcclib31/src -D__CMS__ -D__CHAR_UNSIGNED__`). Not yet run on real CMS.

## Differences from the REXX version (intentional)

* Text is a real character array: `rho`, `take`, `drop`, `reverse`, `cat`,
  `index`, `=` and `<>` work on it (REXX gave DOMAIN ERROR).
* `scan` is the true APL scan (reduction of each prefix, right to left) and
  works along the last axis of any rank; REXX did a left-running total
  that differs for `minus` and `divide`, and rejected matrices.
* Any rank up to 8 for `rho`, `take`, `drop`, `reverse`, `rotate`,
  `transpose` (reverses axes), `reduce`, `outer`, `matmul` (inner product of
  last axis with first axis), `index`, `compress`.
* Function headers are shown with their locals (`Z<-AVG V;S;N`).
* Decimal matrices are right-aligned per column.
* Number literals accept exponents (`1e3`); integers print up to 1e18.
* `)SAVE` writes text variables correctly; deep recursion works (the REXX
  version crashed on `RFACT`).
* `iota`, `rho` etc. accept one-element vectors where a scalar is meant.

## Not done yet

Complex numbers, `[]FMT`, selective assignment, operators written with a
function header (use `dop`), files and screen processors (AP210, AP124,
AP126 are reserved numbers), and the CMS build (deck step for lines over
71 columns, `setjmp` declaration, Hercules test).  Chapter 10 of the manual
has the full list.

## Nested arrays (APL2) -- v0.2

Arrays may now contain arrays.  Strand notation builds them:

    y <- (1 2) (3 4 5) 'abc'        # three-item vector
    depth y                         # 2
    each rho y                      # 2 3 3

New words: `enclose` `disclose` `first` `pick` `match` `depth` `tally`
`member` `enlist`.  Scalar functions are pervasive (`(1 2) (3 4) plus 1`),
`cat` mixes types into a nested vector, and `each`, `reduce`, `scan` and
`outer` accept any item (`each` may return arrays).  `pick` takes a path:
`2 pick y` takes an item; `2 1 pick y` picks item 2, then item 1 of that;
`(enclose 1 2) pick m` indexes a matrix.  A reduction of a vector gives a scalar,
so `reduce plus (1 2) (3 4)` is the *enclosed* vector `4 6` (shown with a
leading blank), as in APL2.  Workspaces save nested values as strand
expressions.  Flat arrays still use the original fast paths; results that
are all simple scalars are demoted automatically.
Display spacing of nested character items is unverified against APL2.


## Language completion -- v0.3

**Scalar functions** (all pervasive through nested arrays):
`power` (`exp` monadic), `log` (`ln` monadic), `fact` (factorial / binomial),
`circle` (circular functions 0..7 and negatives; monadic = pi times),
`signum` / `recip` (also monadic `times` / `divide`), `nand` `nor`,
`and` / `or` are LCM / GCD (boolean on 0/1), `roll` and `deal`.
Comparison uses `[]CT` (default 1e-13, settable).  Results outside +-7e75
(the hex-float limit on VM/370) are a DOMAIN ERROR on every platform, so
behaviour does not depend on the machine.

**Structural**: `gradeup` `gradedown` (monadic, or `collating gradeup text`),
`indexof` (dyadic iota), `unique` `nubsieve` `union` `intersect` `without`
`find` `where` `split` `mix` `expand` `partition` `encode` `decode`
`domino` (inverse / solve / least squares), general `transpose`
(`1 1 transpose m` is the diagonal), `iota 2 3` (index vectors), `right` /
`left`.  `compress` is APL2 replicate (counts, negative = fill).
`cat`, `compress`, `expand`, `reverse`, `rotate` work along the **last**
axis by default; `reverse[1]`, `cat[1]` ... select another axis.

**Brackets**: `v[2]`, `m[1;2]`, `m[;3]`, `m[1 3;]`, `v[i] <- x`,
`m[2;] <- 0`; axis after a function: `reduce plus[1] m`, `1 rotate[1] m`.
A bracket after a numeric literal indexes the whole literal: `1 2 3[2]`.

**Operators** (prefix form, optionally with a left argument):
`x each f y`, `n reduce f v` (n-wise), `x inner f g y`, `x outer f y`,
`swap f` (commute), `repeat f n y` (power), `until f g y`,
`rank f k y` (cells of rank k; `rank f (l r) y` for dyadic).

**Lambdas**: `sq <- { omega times omega }`, `x { alpha plus omega } y`,
statements separated by `;`, guards `cond : value`, local assignment,
`self` for recursion.  A lambda can be an operand: `each { omega plus 1 } v`.
Named lambdas are listed by `)FNS`, saved by `)SAVE`, and fetched with
`)COPY`.

**System**: `format` (monadic display text; `p format x` / `(w p) format x`),
`execute`, `[]CT` `[]RL` `[]A` `[]D`, `[]NC` `[]EX` `[]CR` `[]FX`,
`)COPY ws [name ...]`.

New words are reserved (cannot be variable names): everything listed by
`)PRIMS`, plus `alpha`/`omega`/`self` inside lambdas.

Not done: complex numbers, user-defined operators, `[]FMT`, selective
assignment, multi-line `:If`/`:While` control words.

## Language completion and hooks -- v0.4

* **Fixes**: `a mod b` is APL residue (the left argument is the modulus);
  reducing an empty array gives the identity element (`reduce plus zilch` is
  0); `0 divide 0` is 1; `[]EA`, `[]SIGNAL`, `[]DM`, `[]EN`.
* **Control words** (one per line inside a `nabla` function):
  `:If :ElseIf :Else :EndIf`, `:While`, `:Repeat :Until`, `:For v :In x`,
  `:Select :Case :CaseList :Else`, `:Leave :Continue :Return`.
* **New words**: `zilch` `notmatch` `ucs` `nest`; operators `over` `atop`
  `amend` `keyed`; user-defined operators `name <- dop { ... alphaalpha ...
  omegaomega ... }`.
* **System**: `[]AV` `[]TC` `[]WA` `[]AI` `[]DM` `[]EN`, `[]SIGNAL`, `[]DL`,
  `[]VFI`.
* **Shared variables / auxiliary processors** (`src/p3b_ap.c`): `n []SVO
  'NAME'`, `[]SVO`, `[]SVR`, `[]SVQ`, `)PROCS`.  A processor is four C hooks
  (offer / get / set / retract) registered in `ap_init`.  Demonstration
  processors: 1 LOOPBACK, 2 UPPER, 3 COUNTER; 124, 126 and 210 are reserved.
* **Manual**: `docs/APL-User-Guide.docx` (`sh docs/make_manual.sh`).
