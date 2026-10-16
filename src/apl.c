/* apl.c - word-glyph APL interpreter, portable C89 (single translation
 * unit).  This file includes the parts in order; tools/amalg.sh inlines
 * them into one self-contained file for platforms that want a single
 * source (e.g. the CMS loader).
 */
#include "p0_base.c"
#include "p1_val.c"
#include "p2_lex.c"
#include "p3_prim.c"
#include "p3b_ap.c"
#include "p3c_fmt.c"
#include "p4_eval.c"
#include "p4b_oo.c"
#include "p5_sys.c"
#include "p5b_oosys.c"
