/* apl.c - word-glyph APL interpreter, portable C89 (single translation
 * unit).  This file includes the parts in order; tools/amalg.sh inlines
 * them into one self-contained file for platforms that want a single
 * source (e.g. the CMS loader).
 */
/* ===== p0_base.c ===== */
/* p0_base.c - portable base layer: includes, types, memory, errors.
 * Strict C89. No codepoint arithmetic (EBCDIC-safe). No 64-bit ints.
 */
#if defined(__unix__) || defined(__unix) || defined(__linux__) || \
    defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#define APL_HAVE_DIRENT 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>

#ifdef __CMS__
/* GCCLIB31's setjmp.h declares only jmp_buf; the routines are in assembler */
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif

#define APL_VERSION "0.1"
#ifndef APL_MAXELEMS
#define APL_MAXELEMS 16777216L   /* largest array, in elements */
#endif
#define MAXRANK 8
#define MAXLINE 1024
#define MAXCATCH 32
#define MAXDEPTH 200

/* ---- character classes (never use ranges for letters: EBCDIC) ---- */
static const char LETTERS[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char LOWER[] = "abcdefghijklmnopqrstuvwxyz";

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c) { return c != 0 && strchr(LETTERS, c) != NULL; }
static int is_alnum(int c) { return is_digit(c) || is_alpha(c); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

static int to_upper(int c)
{
  const char *p = (c != 0) ? strchr(LOWER, c) : NULL;
  return p ? UPPER[p - LOWER] : c;
}

/* ---- error handling: setjmp/longjmp catch stack ---- */
static jmp_buf catch_jb[MAXCATCH];
static int catch_top = -1;
static char errkind[32];
static char errmsg[256];

static void unwind_frames(int depth);   /* defined in the evaluator */

static void fail(const char *kind, const char *msg)
{
  strncpy(errkind, kind, sizeof(errkind) - 1);
  errkind[sizeof(errkind) - 1] = '\0';
  strncpy(errmsg, msg, sizeof(errmsg) - 1);
  errmsg[sizeof(errmsg) - 1] = '\0';
  if (catch_top < 0) {
    fprintf(stderr, "apl: %s error -- %s\n", errkind, errmsg);
    exit(2);
  }
  longjmp(catch_jb[catch_top], 1);
}
static void rt_error(const char *msg) { fail("runtime", msg); }
static void syn_error(const char *msg) { fail("syntax", msg); }
static void rt_error_s(const char *a, const char *b, const char *c)
{
  char buf[256];
  sprintf(buf, "%.60s%.100s%.60s", a, b, c);
  fail("runtime", buf);
}
static void syn_error_s(const char *a, const char *b)
{
  char buf[256];
  sprintf(buf, "%.80s%.120s", a, b);
  fail("syntax", buf);
}

/* ---- memory ---- */
static void *xmalloc(size_t n)
{
  void *p = malloc(n ? n : 1);
  if (!p) rt_error("WS FULL");
  return p;
}
static void *xrealloc(void *q, size_t n)
{
  void *p = realloc(q, n ? n : 1);
  if (!p) rt_error("WS FULL");
  return p;
}
static char *xstrdup(const char *s)
{
  size_t n = strlen(s);
  char *p = (char *)xmalloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}
static char *xstrndup(const char *s, int n)
{
  char *p = (char *)xmalloc((size_t)n + 1);
  memcpy(p, s, (size_t)n);
  p[n] = '\0';
  return p;
}

/* ---- interpreter-wide settings ---- */
static int Q_IO = 1;          /* []IO */
static int Q_PP = 6;          /* []PP */
#define APL_NUM_MAX 7.0e75   /* hex FP on VM/370 tops out near 7.2e75 */
static double Q_CT = 1e-13;   /* []CT comparison tolerance */
static unsigned long Q_RL = 16807UL; /* []RL random seed (32 bit) */
static char wsid[128] = "CLEAR WS";
static int interactive = 0;   /* 1 when reading a terminal REPL */
/* ===== p1_val.c ===== */
/* p1_val.c - array values: allocation, refcounts, nesting, formatting, display
 *
 * Model (APL2): an array has a rank, a shape and items.  An item is itself
 * an array.  T_NUM and T_CHR arrays hold simple scalars in flat C arrays
 * (fast path); a T_NEST array holds an Arr* per item (an item that is a
 * simple scalar is a rank-0 T_NUM/T_CHR array).  Operations that build
 * T_NEST results pass them through a_simplify() so an array whose items are
 * all simple scalars of one kind is always stored flat.
 */

#define T_NUM 0
#define T_CHR 1
#define T_NEST 2

typedef struct Arr {
  struct Arr *next, *prev;     /* list of all live arrays (for sweeping) */
  int refc;
  int type;
  int rank;
  int n;                       /* element count */
  int shape[MAXRANK];
  double *num;                 /* T_NUM data */
  char *chr;                   /* T_CHR data (n+1 bytes, NUL terminated) */
  struct Arr **sub;            /* T_NEST items */
  int mark;
} Arr;

static Arr *all_arrs = NULL;

static int shape_count(int rank, const int *shape)
{
  double c = 1.0;
  int i;
  for (i = 0; i < rank; i++) c *= (double)shape[i];
  if (c > (double)APL_MAXELEMS) rt_error("WS FULL");
  return (int)c;
}

static Arr *a_new(int type, int rank, const int *shape)
{
  Arr *a = (Arr *)xmalloc(sizeof(Arr));
  int i;
  a->refc = 1;
  a->type = type;
  a->rank = rank;
  for (i = 0; i < MAXRANK; i++) a->shape[i] = (i < rank) ? shape[i] : 0;
  a->n = shape_count(rank, shape);
  a->num = NULL;
  a->chr = NULL;
  a->sub = NULL;
  a->mark = 0;
  if (type == T_NUM) {
    a->num = (double *)xmalloc(sizeof(double) * (size_t)a->n);
  } else if (type == T_CHR) {
    a->chr = (char *)xmalloc((size_t)a->n + 1);
    a->chr[a->n] = '\0';
  } else {
    a->sub = (Arr **)xmalloc(sizeof(Arr *) * (size_t)a->n);
    for (i = 0; i < a->n; i++) a->sub[i] = NULL;
  }
  a->prev = NULL;
  a->next = all_arrs;
  if (all_arrs) all_arrs->prev = a;
  all_arrs = a;
  return a;
}

static void a_unlink(Arr *a)
{
  if (a->prev) a->prev->next = a->next; else all_arrs = a->next;
  if (a->next) a->next->prev = a->prev;
}
static void a_rel(Arr *a);
static void a_free(Arr *a)
{
  a_unlink(a);
  if (a->sub) {
    int i;
    for (i = 0; i < a->n; i++) a_rel(a->sub[i]);
    free(a->sub);
  }
  free(a->num);
  free(a->chr);
  free(a);
}
static Arr *a_ref(Arr *a) { if (a) a->refc++; return a; }
static void a_rel(Arr *a)
{
  if (a && --a->refc <= 0) a_free(a);
}

static Arr *a_scalar(double v)
{
  Arr *a = a_new(T_NUM, 0, NULL);
  a->num[0] = v;
  return a;
}
static Arr *a_char(int c)
{
  Arr *a = a_new(T_CHR, 0, NULL);
  a->chr[0] = (char)c;
  return a;
}
static Arr *a_vec(int type, int n)
{
  int sh[1];
  sh[0] = n;
  return a_new(type, 1, sh);
}
static Arr *a_str(const char *s)
{
  int n = (int)strlen(s);
  Arr *a = a_vec(T_CHR, n);
  memcpy(a->chr, s, (size_t)n);
  return a;
}

static int a_simple_scalar(const Arr *a)
{
  return a->rank == 0 && a->type != T_NEST;
}

/* an owned reference to item i (a rank-0 array for simple elements) */
static Arr *a_item(const Arr *a, int i)
{
  if (a->type == T_NUM) return a_scalar(a->num[i]);
  if (a->type == T_CHR) return a_char((unsigned char)a->chr[i]);
  return a_ref(a->sub[i]);
}

/* store an item into a T_NEST array (takes ownership of item) */
static void a_put(Arr *d, int i, Arr *item)
{
  a_rel(d->sub[i]);
  d->sub[i] = item;
}

/* copy element si of s to element di of d.  d's type decides the form. */
static void a_copyel(Arr *d, int di, const Arr *s, int si)
{
  if (d->type == T_NUM) d->num[di] = s->num[si];
  else if (d->type == T_CHR) d->chr[di] = s->chr[si];
  else a_put(d, di, a_item(s, si));
}
static void a_fill(Arr *d, int di)       /* prototype fill: 0 or blank */
{
  if (d->type == T_NUM) d->num[di] = 0.0;
  else if (d->type == T_CHR) d->chr[di] = ' ';
  else {
    int blank = 0;
    if (d->n > 0 && di != 0 && d->sub[0] && d->sub[0]->type == T_CHR &&
        d->sub[0]->rank == 0) blank = 1;
    a_put(d, di, blank ? a_char(' ') : a_scalar(0.0));
  }
}
static Arr *a_like(const Arr *x, int rank, const int *shape)
{
  return a_new(x->type, rank, shape);
}
static int a_same_shape(const Arr *x, const Arr *y)
{
  int i;
  if (x->rank != y->rank) return 0;
  for (i = 0; i < x->rank; i++) if (x->shape[i] != y->shape[i]) return 0;
  return 1;
}

/* a T_NEST copy-or-reference of x with the same shape */
static Arr *a_to_nest(Arr *x)
{
  Arr *r;
  int i;
  if (x->type == T_NEST) return a_ref(x);
  r = a_new(T_NEST, x->rank, x->shape);
  for (i = 0; i < x->n; i++) a_put(r, i, a_item(x, i));
  return r;
}

/* demote a T_NEST array whose items are all simple scalars of one kind */
static Arr *a_simplify(Arr *a)
{
  int i, t = -1;
  Arr *r;
  if (a->type != T_NEST) return a;
  for (i = 0; i < a->n; i++) {
    Arr *s = a->sub[i];
    if (!s || !a_simple_scalar(s)) return a;
    if (t < 0) t = s->type;
    else if (t != s->type) return a;
  }
  if (t < 0) t = T_NUM;                 /* empty: numeric */
  r = a_new(t, a->rank, a->shape);
  for (i = 0; i < a->n; i++) {
    if (t == T_NUM) r->num[i] = a->sub[i]->num[0];
    else r->chr[i] = a->sub[i]->chr[0];
  }
  a_rel(a);
  return r;
}

/* deep match (APL2 match) */
static int a_match(const Arr *a, const Arr *b)
{
  int i;
  if (!a_same_shape(a, b)) return 0;
  if (a->type != T_NEST && b->type == a->type) {
    for (i = 0; i < a->n; i++)
      if (a->type == T_NUM ? a->num[i] != b->num[i] : a->chr[i] != b->chr[i])
        return 0;
    return 1;
  }
  if (a->type != T_NEST && b->type != T_NEST) return a->n == 0 && b->n == 0;
  for (i = 0; i < a->n; i++) {
    Arr *x = a_item(a, i), *y = a_item(b, i);
    int same;
    same = (x == y) ? 1 : a_match(x, y);
    a_rel(x); a_rel(y);
    if (!same) return 0;
  }
  return 1;
}

static int a_depth(const Arr *a)
{
  int i, d = 0;
  if (a->type != T_NEST) return a->rank == 0 ? 0 : 1;
  for (i = 0; i < a->n; i++) {
    int k = a_depth(a->sub[i]);
    if (k > d) d = k;
  }
  return d + 1;
}

/* mark & sweep, used only at top level after an error (no live temporaries) */
static void mark_arr(Arr *a)
{
  int i;
  if (!a || a->mark) return;
  a->mark = 1;
  if (a->sub) for (i = 0; i < a->n; i++) mark_arr(a->sub[i]);
}
static void sweep_unmarked(void)
{
  Arr *a, *nx;
  int i;
  /* pass 1: dead parents give up their references to their items */
  for (a = all_arrs; a; a = a->next)
    if (!a->mark && a->sub)
      for (i = 0; i < a->n; i++)
        if (a->sub[i]) a->sub[i]->refc--;
  /* pass 2: free the dead without touching the items again */
  a = all_arrs;
  while (a) {
    nx = a->next;
    if (!a->mark) {
      a_unlink(a);
      free(a->sub); free(a->num); free(a->chr); free(a);
    } else a->mark = 0;
    a = nx;
  }
}

/* ---- numbers to text ---- */
static void fmt_num(double x, char *buf)
{
  char *p;
  if (x == 0.0) { strcpy(buf, "0"); return; }
  if (x == floor(x) && fabs(x) < 1e18) {
    sprintf(buf, "%.0f", x);
    return;
  }
  if (fabs(x) >= 1e18 || fabs(x) < 1e-10) {
    sprintf(buf, "%.*g", Q_PP > 0 ? Q_PP : 1, x);
    return;
  }
  sprintf(buf, "%.*f", Q_PP, x);
  if (strchr(buf, '.')) {
    p = buf + strlen(buf) - 1;
    while (p > buf && *p == '0') *p-- = '\0';
    if (*p == '.') *p = '\0';
  }
  if (strcmp(buf, "-0") == 0) strcpy(buf, "0");
}

/* ---- display: rendered as blocks of text lines ---- */
typedef struct Blk { int h, w; char **ln; } Blk;

static Blk *blk_new(int h, int w)
{
  Blk *b = (Blk *)xmalloc(sizeof(Blk));
  int i;
  b->h = h; b->w = w;
  b->ln = (char **)xmalloc(sizeof(char *) * (size_t)(h ? h : 1));
  for (i = 0; i < h; i++) {
    b->ln[i] = (char *)xmalloc((size_t)w + 1);
    memset(b->ln[i], ' ', (size_t)w);
    b->ln[i][w] = '\0';
  }
  return b;
}
static void blk_free(Blk *b)
{
  int i;
  for (i = 0; i < b->h; i++) free(b->ln[i]);
  free(b->ln);
  free(b);
}
static Blk *blk_text(const char *s)
{
  Blk *b = blk_new(1, (int)strlen(s));
  memcpy(b->ln[0], s, strlen(s));
  return b;
}
/* put src into dst at (r, c), left aligned inside a w-wide cell */
static void blk_blit(Blk *dst, int r, int c, const Blk *src, int cellw,
                     int right)
{
  int i, off = right ? cellw - src->w : 0;
  for (i = 0; i < src->h; i++)
    memcpy(dst->ln[r + i] + c + off, src->ln[i], (size_t)src->w);
}
static Blk *blk_pad(Blk *b)
{
  Blk *r = blk_new(b->h, b->w + 2);
  blk_blit(r, 0, 1, b, b->w, 0);
  blk_free(b);
  return r;
}

static Blk *render(const Arr *a);

/* stack plane blocks vertically with the APL separator lines */
static Blk *blk_planes(Blk **pl, int np, const Arr *a)
{
  int lead = a->rank - 2, p, h = 0, w = 0, r, *sep;
  Blk *res;
  sep = (int *)xmalloc(sizeof(int) * (size_t)(np + 1));
  for (p = 0; p < np; p++) {
    int prod = 1, ax, s = 1;
    if (p > 0) {
      for (ax = lead - 1; ax >= 1; ax--) {
        prod *= a->shape[ax];
        if (prod > 0 && p % prod == 0) s++; else break;
      }
    } else s = 0;
    sep[p] = s;
    h += pl[p]->h + s;
    if (pl[p]->w > w) w = pl[p]->w;
  }
  res = blk_new(h, w);
  r = 0;
  for (p = 0; p < np; p++) {
    r += sep[p];
    blk_blit(res, r, 0, pl[p], pl[p]->w, 0);
    r += pl[p]->h;
    blk_free(pl[p]);
  }
  free(sep);
  free(pl);
  return res;
}

static Blk *render_simple(const Arr *a)
{
  char buf[64];
  int i, r, c, k, rows, cols, plane, np, p;
  Blk **pl;
  if (a->rank == 0) {
    if (a->type == T_CHR) { buf[0] = a->chr[0]; buf[1] = '\0'; }
    else fmt_num(a->num[0], buf);
    return blk_text(buf);
  }
  if (a->rank == 1) {
    if (a->type == T_CHR) {
      Blk *b = blk_new(1, a->n);
      memcpy(b->ln[0], a->chr, (size_t)a->n);
      return b;
    } else {
      char *s = (char *)xmalloc((size_t)a->n * 40 + 2);
      Blk *b;
      s[0] = '\0';
      k = 0;
      for (i = 0; i < a->n; i++) {
        if (i) s[k++] = ' ';
        fmt_num(a->num[i], buf);
        strcpy(s + k, buf);
        k += (int)strlen(buf);
      }
      b = blk_text(s);
      free(s);
      return b;
    }
  }
  rows = a->shape[a->rank - 2];
  cols = a->shape[a->rank - 1];
  plane = rows * cols;
  np = (plane > 0) ? a->n / plane : 0;
  if (a->n == 0) return blk_new(1, 0);
  pl = (Blk **)xmalloc(sizeof(Blk *) * (size_t)np);
  for (p = 0; p < np; p++) {
    if (a->type == T_CHR) {
      Blk *b = blk_new(rows, cols);
      for (r = 0; r < rows; r++)
        memcpy(b->ln[r], a->chr + p * plane + r * cols, (size_t)cols);
      pl[p] = b;
    } else {
      char **cell = (char **)xmalloc(sizeof(char *) * (size_t)plane);
      int *w = (int *)xmalloc(sizeof(int) * (size_t)cols), tot = 0;
      Blk *b;
      for (c = 0; c < cols; c++) w[c] = 0;
      for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
          int len;
          fmt_num(a->num[p * plane + r * cols + c], buf);
          cell[r * cols + c] = xstrdup(buf);
          len = (int)strlen(buf);
          if (len > w[c]) w[c] = len;
        }
      for (c = 0; c < cols; c++) tot += w[c] + (c ? 1 : 0);
      b = blk_new(rows, tot);
      for (r = 0; r < rows; r++) {
        int x = 0;
        for (c = 0; c < cols; c++) {
          int len = (int)strlen(cell[r * cols + c]);
          if (c) x++;
          memcpy(b->ln[r] + x + (w[c] - len), cell[r * cols + c], (size_t)len);
          x += w[c];
          free(cell[r * cols + c]);
        }
      }
      free(cell); free(w);
      pl[p] = b;
    }
  }
  return blk_planes(pl, np, a);
}

static Blk *render_nest(const Arr *a)
{
  Blk **cell, **pl, *res;
  int *right;
  int i, r, c, p, np, rows, cols, plane, tot, y;
  int *rh, *cw;
  if (a->rank == 0) return blk_pad(render(a->sub[0]));
  if (a->n == 0) return blk_new(1, 0);
  cell = (Blk **)xmalloc(sizeof(Blk *) * (size_t)a->n);
  right = (int *)xmalloc(sizeof(int) * (size_t)a->n);
  for (i = 0; i < a->n; i++) {
    Arr *it = a->sub[i];
    cell[i] = render(it);
    right[i] = (it->rank == 0 && it->type == T_NUM);
    if (!a_simple_scalar(it)) cell[i] = blk_pad(cell[i]);
  }
  cols = a->shape[a->rank - 1];
  rows = (a->rank >= 2) ? a->shape[a->rank - 2] : 1;
  plane = rows * cols;
  np = (plane > 0) ? a->n / plane : 0;
  pl = (Blk **)xmalloc(sizeof(Blk *) * (size_t)np);
  rh = (int *)xmalloc(sizeof(int) * (size_t)rows);
  cw = (int *)xmalloc(sizeof(int) * (size_t)cols);
  for (p = 0; p < np; p++) {
    Blk **pc = cell + p * plane;
    int *pr = right + p * plane;
    for (r = 0; r < rows; r++) rh[r] = 1;
    for (c = 0; c < cols; c++) cw[c] = 0;
    for (r = 0; r < rows; r++)
      for (c = 0; c < cols; c++) {
        Blk *b = pc[r * cols + c];
        if (b->h > rh[r]) rh[r] = b->h;
        if (b->w > cw[c]) cw[c] = b->w;
      }
    tot = 0;
    for (c = 0; c < cols; c++) tot += cw[c] + (c ? 1 : 0);
    y = 0;
    for (r = 0; r < rows; r++) y += rh[r];
    res = blk_new(y, tot);
    y = 0;
    for (r = 0; r < rows; r++) {
      int x = 0;
      for (c = 0; c < cols; c++) {
        if (c) x++;
        blk_blit(res, y, x, pc[r * cols + c], cw[c], pr[r * cols + c]);
        x += cw[c];
      }
      y += rh[r];
    }
    pl[p] = res;
  }
  for (i = 0; i < a->n; i++) blk_free(cell[i]);
  free(cell); free(right); free(rh); free(cw);
  if (a->rank <= 2) { res = pl[0]; free(pl); return res; }
  return blk_planes(pl, np, a);
}

static Blk *render(const Arr *a)
{
  return (a->type == T_NEST) ? render_nest(a) : render_simple(a);
}

static void show(const Arr *a)
{
  Blk *b = render(a);
  int i, n;
  for (i = 0; i < b->h; i++) {
    n = (int)strlen(b->ln[i]);
    while (n > 0 && b->ln[i][n - 1] == ' ') n--;
    fwrite(b->ln[i], 1, (size_t)n, stdout);
    putchar('\n');
  }
  blk_free(b);
}
/* ===== p2_lex.c ===== */
/* p2_lex.c - tokenizer */

#define K_END 0
#define K_NUM 1
#define K_STR 2
#define K_NAME 3
#define K_SYM 4
#define K_QUAD 5       /* bare [] */
#define K_QNAME 6      /* []NAME  (s holds NAME, as typed) */

typedef struct Tok {
  int k;
  double num;
  char *s;
} Tok;

/* temporaries that must be released if an error unwinds past them */
typedef struct Trk { void *p; int is_tok; } Trk;
static Trk *trk = NULL;
static int ntrk = 0, captrk = 0;

static void trk_add(void *p, int is_tok)
{
  if (ntrk == captrk) {
    captrk = captrk ? captrk * 2 : 32;
    trk = (Trk *)xrealloc(trk, sizeof(Trk) * (size_t)captrk);
  }
  trk[ntrk].p = p; trk[ntrk].is_tok = is_tok;
  ntrk++;
}
static void trk_del(void *p)
{
  int i;
  for (i = ntrk - 1; i >= 0; i--)
    if (trk[i].p == p) {
      for (; i + 1 < ntrk; i++) trk[i] = trk[i + 1];
      ntrk--;
      return;
    }
}

static void lex_free(Tok *t)
{
  int i;
  if (!t) return;
  trk_del(t);
  for (i = 0; t[i].k != K_END; i++) free(t[i].s);
  free(t);
}

/* Is the span at p a number literal start? (digit, or . digit) */
static int num_start(const char *p)
{
  return is_digit(p[0]) || (p[0] == '.' && is_digit(p[1]));
}

static int num_len(const char *p)
{
  int i = 0;
  while (is_digit(p[i])) i++;
  if (p[i] == '.') {
    i++;
    while (is_digit(p[i])) i++;
  }
  if (p[i] == 'e' || p[i] == 'E') {
    int j = i + 1;
    if (p[j] == '+' || p[j] == '-') j++;
    if (is_digit(p[j])) {
      while (is_digit(p[j])) j++;
      i = j;
    }
  }
  return i;
}

static Tok *lex(const char *line)
{
  int cap = 16, n = 0;
  Tok *t = (Tok *)xmalloc(sizeof(Tok) * (size_t)cap);
  const char *p = line;
  while (1) {
    int k = K_END, len = 0;
    double v = 0.0;
    char *s = NULL;
    while (is_space((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '#') break;
    if (is_alpha((unsigned char)*p)) {
      const char *q = p;
      while (is_alnum((unsigned char)*q)) q++;
      len = (int)(q - p);
      if (len == 4 && strncmp(p, "lamp", 4) == 0) break;   /* comment */
      k = K_NAME;
      s = xstrndup(p, len);
      p = q;
    } else if (num_start(p) ||
               (*p == '-' && num_start(p + 1))) {
      int neg = (*p == '-');
      const char *b = neg ? p + 1 : p;
      char *tmp;
      len = num_len(b);
      tmp = xstrndup(b, len);
      v = strtod(tmp, NULL);
      free(tmp);
      if (neg) v = -v;
      k = K_NUM;
      p = b + len;
    } else if (*p == '\'') {
      const char *q = p + 1;
      int m = 0;
      char *buf = (char *)xmalloc(strlen(q) + 1);
      while (1) {
        if (*q == '\0') { free(buf); syn_error("unterminated string"); }
        if (*q == '\'') {
          if (q[1] == '\'') { buf[m++] = '\''; q += 2; continue; }
          q++;
          break;
        }
        buf[m++] = *q++;
      }
      buf[m] = '\0';
      k = K_STR;
      s = buf;
      p = q;
    } else if (*p == '[' && p[1] == ']') {
      const char *q = p + 2;
      if (is_alpha((unsigned char)*q)) {
        const char *b = q;
        while (is_alnum((unsigned char)*q)) q++;
        k = K_QNAME;
        s = xstrndup(b, (int)(q - b));
      } else {
        k = K_QUAD;
      }
      p = q;
    } else {
      const char *sym = NULL;
      if (p[0] == '<' && p[1] == '-') sym = "<-";
      else if (p[0] == '-' && p[1] == '>') sym = "->";
      else if (p[0] == '<' && p[1] == '=') sym = "<=";
      else if (p[0] == '>' && p[1] == '=') sym = ">=";
      else if (p[0] == '<' && p[1] == '>') sym = "<>";
      if (sym) {
        k = K_SYM; s = xstrdup(sym); p += 2;
      } else if (strchr("+-*/<>=();:[]{}", *p)) {
        char one[2];
        one[0] = *p; one[1] = '\0';
        k = K_SYM; s = xstrdup(one); p++;
      } else {
        char buf[64];
        sprintf(buf, "unexpected token near %.20s", p);
        { Tok e; e.k = K_END; e.s = NULL; e.num = 0; t[n] = e; }
        lex_free(t);
        syn_error(buf);
      }
    }
    if (n + 2 > cap) {
      cap *= 2;
      t = (Tok *)xrealloc(t, sizeof(Tok) * (size_t)cap);
    }
    t[n].k = k; t[n].num = v; t[n].s = s;
    n++;
    t[n].k = K_END; t[n].s = NULL; t[n].num = 0;
  }
  t[n].k = K_END; t[n].s = NULL; t[n].num = 0;
  return t;
}

/* lex a temporary statement: freed by lex_free, or by trk_unwind on error */
static Tok *lex_t(const char *line)
{
  Tok *t = lex(line);
  trk_add(t, 1);
  return t;
}
static void trk_unwind(int mark)
{
  while (ntrk > mark) {
    Trk e = trk[--ntrk];
    if (e.is_tok) {
      Tok *t = (Tok *)e.p;
      int i;
      for (i = 0; t[i].k != K_END; i++) free(t[i].s);
      free(t);
    } else free(e.p);
  }
}

static int tok_sym(const Tok *t, const char *s)
{
  return t->k == K_SYM && strcmp(t->s, s) == 0;
}
static int tok_name(const Tok *t, const char *s)
{
  return t->k == K_NAME && strcmp(t->s, s) == 0;
}
/* ===== p3_prim.c ===== */
/* p3_prim.c - primitive functions (scalar and structural) */

enum {
  P_ADD, P_SUB, P_MUL, P_DIV, P_EQ, P_LT, P_GT, P_LE, P_GE, P_NE,
  P_MAX, P_MIN, P_AND, P_OR, P_MOD,
  P_POW, P_LOG, P_BINOM, P_CIRCLE, P_NAND, P_NOR,
  P_ABS, P_FLOOR, P_CEIL, P_NOT, P_ROLL,
  P_RHO, P_IOTA, P_RAVEL, P_REVERSE, P_ROTATE, P_TRANSPOSE,
  P_TAKE, P_DROP, P_CAT, P_COMPRESS, P_INDEX, P_MATMUL,
  P_ENCLOSE, P_DISCLOSE, P_FIRST, P_PICK, P_MATCH, P_DEPTH, P_TALLY,
  P_MEMBER, P_ENLIST, P_DEAL,
  P_EXPAND, P_GRADEUP, P_GRADEDN, P_INDEXOF, P_UNIQUE, P_NUBSIEVE,
  P_UNION, P_INTERSECT, P_WITHOUT, P_FIND, P_WHERE, P_SPLIT,
  P_RIGHT, P_LEFT, P_FORMAT, P_EXECUTE, P_ENCODE, P_DECODE, P_DOMINO,
  P_PARTITION, P_NOTMATCH, P_UCS, P_NEST,
  P_REDUCE, P_SCAN, P_EACH, P_OUTER, P_INNER, P_SWAP, P_REPEAT, P_UNTIL,
  P_RANK, P_OVER, P_ATOP, P_AMEND, P_KEYED,
  P_COUNT
};

typedef struct PrimDef {
  const char *name;
  int id;
  int mon;
  int dya;
} PrimDef;

static const PrimDef PRIMS[] = {
  { "+", P_ADD, 1, 1 },   { "plus", P_ADD, 1, 1 },
  { "-", P_SUB, 1, 1 },   { "minus", P_SUB, 1, 1 },
  { "neg", P_SUB, 1, 0 },
  { "*", P_MUL, 0, 1 },   { "times", P_MUL, 1, 1 },
  { "/", P_DIV, 0, 1 },   { "divide", P_DIV, 1, 1 },
  { "signum", P_MUL, 1, 0 }, { "recip", P_DIV, 1, 0 },
  { "power", P_POW, 1, 1 }, { "exp", P_POW, 1, 0 },
  { "log", P_LOG, 1, 1 }, { "ln", P_LOG, 1, 0 },
  { "fact", P_BINOM, 1, 1 }, { "circle", P_CIRCLE, 1, 1 },
  { "nand", P_NAND, 0, 1 }, { "nor", P_NOR, 0, 1 },
  { "roll", P_ROLL, 1, 0 }, { "deal", P_DEAL, 0, 1 },
  { "=", P_EQ, 0, 1 },    { "<", P_LT, 0, 1 },  { ">", P_GT, 0, 1 },
  { "<=", P_LE, 0, 1 },   { ">=", P_GE, 0, 1 }, { "<>", P_NE, 0, 1 },
  { "max", P_MAX, 0, 1 }, { "min", P_MIN, 0, 1 },
  { "and", P_AND, 0, 1 }, { "or", P_OR, 0, 1 }, { "mod", P_MOD, 0, 1 },
  { "abs", P_ABS, 1, 0 }, { "floor", P_FLOOR, 1, 0 },
  { "ceil", P_CEIL, 1, 0 }, { "not", P_NOT, 1, 0 },
  { "rho", P_RHO, 1, 1 }, { "iota", P_IOTA, 1, 0 },
  { "ravel", P_RAVEL, 1, 0 }, { "reverse", P_REVERSE, 1, 0 },
  { "rotate", P_ROTATE, 0, 1 }, { "transpose", P_TRANSPOSE, 1, 1 },
  { "take", P_TAKE, 0, 1 }, { "drop", P_DROP, 0, 1 },
  { "cat", P_CAT, 0, 1 }, { "compress", P_COMPRESS, 0, 1 },
  { "index", P_INDEX, 0, 1 }, { "matmul", P_MATMUL, 0, 1 },
  { "enclose", P_ENCLOSE, 1, 0 }, { "disclose", P_DISCLOSE, 1, 0 },
  { "first", P_FIRST, 1, 0 }, { "pick", P_PICK, 0, 1 },
  { "match", P_MATCH, 0, 1 }, { "depth", P_DEPTH, 1, 0 },
  { "tally", P_TALLY, 1, 0 }, { "member", P_MEMBER, 0, 1 },
  { "enlist", P_ENLIST, 1, 0 }, { "mix", P_DISCLOSE, 1, 0 },
  { "expand", P_EXPAND, 0, 1 }, { "gradeup", P_GRADEUP, 1, 1 },
  { "gradedown", P_GRADEDN, 1, 1 }, { "indexof", P_INDEXOF, 0, 1 },
  { "unique", P_UNIQUE, 1, 0 }, { "nubsieve", P_NUBSIEVE, 1, 0 },
  { "union", P_UNION, 0, 1 }, { "intersect", P_INTERSECT, 0, 1 },
  { "without", P_WITHOUT, 0, 1 }, { "find", P_FIND, 0, 1 },
  { "where", P_WHERE, 1, 0 }, { "split", P_SPLIT, 1, 0 },
  { "right", P_RIGHT, 1, 1 }, { "left", P_LEFT, 1, 1 },
  { "format", P_FORMAT, 1, 1 }, { "execute", P_EXECUTE, 1, 0 },
  { "encode", P_ENCODE, 0, 1 }, { "decode", P_DECODE, 0, 1 },
  { "domino", P_DOMINO, 1, 1 }, { "partition", P_PARTITION, 0, 1 },
  { "notmatch", P_NOTMATCH, 0, 1 }, { "ucs", P_UCS, 1, 0 },
  { "nest", P_NEST, 1, 0 },
  { "reduce", P_REDUCE, 0, 0 }, { "scan", P_SCAN, 0, 0 },
  { "each", P_EACH, 0, 0 }, { "outer", P_OUTER, 0, 0 },
  { "inner", P_INNER, 0, 0 }, { "swap", P_SWAP, 0, 0 },
  { "repeat", P_REPEAT, 0, 0 }, { "until", P_UNTIL, 0, 0 },
  { "rank", P_RANK, 0, 0 },
  { "over", P_OVER, 0, 0 }, { "atop", P_ATOP, 0, 0 },
  { "amend", P_AMEND, 0, 0 }, { "keyed", P_KEYED, 0, 0 },
  { NULL, 0, 0, 0 }
};

static const PrimDef *prim_lookup(const char *name)
{
  int i;
  for (i = 0; PRIMS[i].name; i++)
    if (strcmp(PRIMS[i].name, name) == 0) return &PRIMS[i];
  return NULL;
}
static int is_operator(int id) { return id >= P_REDUCE && id <= P_KEYED; }

static const char *prim_name(int id)
{
  int i;
  for (i = 0; PRIMS[i].name; i++)
    if (PRIMS[i].id == id) return PRIMS[i].name;
  return "?";
}

static void need_num(const Arr *a, int id)
{
  if (a->type == T_NEST)
    rt_error_s("DOMAIN ERROR: ", prim_name(id),
               " does not accept a nested value");
  if (a->type != T_NUM)
    rt_error_s("DOMAIN ERROR: ", prim_name(id),
               " does not accept a text value");
}

/* tolerant comparison */
static int ct_eq(double a, double b)
{
  double m;
  if (a == b) return 1;
  m = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
  return fabs(a - b) <= Q_CT * m;
}

static double gcd_d(double a, double b)
{
  a = fabs(a); b = fabs(b);
  while (b > Q_CT * (a > 1.0 ? a : 1.0) && b != 0.0) {
    double t = fmod(a, b);
    a = b; b = t;
  }
  return a;
}

static double gamma_d(double x)
{
  static const double g[9] = {
    0.99999999999980993, 676.5203681218851, -1259.1392167224028,
    771.32342877765313, -176.61502916214059, 12.507343278686905,
    -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7 };
  if (x == floor(x)) {
    double r = 1.0;
    int i, n;
    if (x <= 0.0) rt_error("DOMAIN ERROR: fact of a non-positive integer");
    if (x > 171.0) rt_error("DOMAIN ERROR: fact overflow");
    n = (int)x;
    for (i = 2; i < n; i++) {
      r *= (double)i;
      if (r > APL_NUM_MAX) rt_error("DOMAIN ERROR: fact overflow");
    }
    return r;
  } else {
    double a, t, s, pi = 3.14159265358979323846;
    int i;
    if (x < 0.5) return pi / (sin(pi * x) * gamma_d(1.0 - x));
    x -= 1.0;
    a = g[0];
    t = x + 7.5;
    for (i = 1; i < 9; i++) a += g[i] / (x + (double)i);
    s = sqrt(2.0 * pi) * pow(t, x + 0.5) * exp(-t) * a;
    return s;
  }
}

static double fact_d(double x)
{
  if (x < 0.0 && x == floor(x)) rt_error("DOMAIN ERROR: fact of a negative integer");
  return gamma_d(x + 1.0);
}

static double binom_d(double a, double b)
{
  /* a ! b : ways of choosing a from b */
  if (a == floor(a) && b == floor(b) && a >= 0.0 && b >= a) {
    double r = 1.0, k = a < b - a ? a : b - a;
    int i, n = (int)k;
    for (i = 1; i <= n; i++) r = r * (b - k + (double)i) / (double)i;
    return floor(r + 0.5);
  }
  if (a == floor(a) && b == floor(b) && a >= 0.0 && b >= 0.0) return 0.0;
  return fact_d(b) / (fact_d(a) * fact_d(b - a));
}

static double circ_d(double k, double x)
{
  double pi = 3.14159265358979323846;
  int c;
  if (k != floor(k)) rt_error("DOMAIN ERROR: circle needs an integer function number");
  c = (int)k;
  switch (c) {
  case 0: if (fabs(x) > 1.0) rt_error("DOMAIN ERROR: circle 0"); return sqrt(1.0 - x * x);
  case 1: return sin(x);
  case 2: return cos(x);
  case 3: return tan(x);
  case 4: if (x * x < 1.0) rt_error("DOMAIN ERROR: circle 4"); return sqrt(x * x - 1.0);
  case 5: return sinh(x);
  case 6: return cosh(x);
  case 7: return tanh(x);
  case -1: if (fabs(x) > 1.0) rt_error("DOMAIN ERROR: circle -1"); return asin(x);
  case -2: if (fabs(x) > 1.0) rt_error("DOMAIN ERROR: circle -2"); return acos(x);
  case -3: return atan(x);
  case -4: if (x * x < 1.0) rt_error("DOMAIN ERROR: circle -4"); return sqrt(x * x - 1.0);
  case -5: return log(x + sqrt(x * x + 1.0));
  case -6: if (x < 1.0) rt_error("DOMAIN ERROR: circle -6"); return log(x + sqrt(x * x - 1.0));
  case -7: if (fabs(x) >= 1.0) rt_error("DOMAIN ERROR: circle -7"); return 0.5 * log((1.0 + x) / (1.0 - x));
  case 9: return x;
  case 10: return fabs(x);
  case 11: return 0.0;
  case 12: return x < 0.0 ? pi : 0.0;
  }
  rt_error("DOMAIN ERROR: unknown circle function number");
  return 0.0;
}

static double rnd_next(void)
{
  Q_RL = (Q_RL ^ (Q_RL << 13)) & 0xFFFFFFFFUL;
  Q_RL ^= Q_RL >> 17;
  Q_RL = (Q_RL ^ (Q_RL << 5)) & 0xFFFFFFFFUL;
  return (double)Q_RL / 4294967296.0;
}

static int as_int(double v, int id)
{
  if (v != floor(v) || fabs(v) > 2000000000.0)
    rt_error_s("DOMAIN ERROR: ", prim_name(id), " needs an integer");
  return (int)v;
}

/* ---------- scalar functions ---------- */
static double sdya0(int id, double a, double b)
{
  switch (id) {
  case P_ADD: return a + b;
  case P_SUB: return a - b;
  case P_MUL: return a * b;
  case P_DIV:
    if (b == 0.0) {
      if (a == 0.0) return 1.0;
      rt_error("DOMAIN ERROR: divide by zero");
    }
    return a / b;
  case P_EQ: return ct_eq(a, b) ? 1.0 : 0.0;
  case P_LT: return (a < b && !ct_eq(a, b)) ? 1.0 : 0.0;
  case P_GT: return (a > b && !ct_eq(a, b)) ? 1.0 : 0.0;
  case P_LE: return (a < b || ct_eq(a, b)) ? 1.0 : 0.0;
  case P_GE: return (a > b || ct_eq(a, b)) ? 1.0 : 0.0;
  case P_NE: return !ct_eq(a, b) ? 1.0 : 0.0;
  case P_MAX: return a > b ? a : b;
  case P_MIN: return a < b ? a : b;
  case P_AND:
    if (a == 0.0 || b == 0.0) return 0.0;
    return fabs(a * b) / gcd_d(a, b);
  case P_OR: return gcd_d(a, b);
  case P_NAND: return (a != 0.0 && b != 0.0) ? 0.0 : 1.0;
  case P_NOR: return (a == 0.0 && b == 0.0) ? 1.0 : 0.0;
  case P_POW:
    if (a == 0.0 && b < 0.0) rt_error("DOMAIN ERROR: power: zero to a negative power");
    if (a < 0.0 && b != floor(b)) rt_error("DOMAIN ERROR: power: negative base, fractional exponent (complex result)");
    return pow(a, b);
  case P_LOG:
    if (a <= 0.0 || b <= 0.0 || a == 1.0) rt_error("DOMAIN ERROR: log");
    return log(b) / log(a);
  case P_BINOM: return binom_d(a, b);
  case P_CIRCLE: return circ_d(a, b);
  case P_MOD:
    /* APL residue: the LEFT argument is the modulus */
    if (a == 0.0) return b;
    {
      double q = b / a, fq = floor(q + 0.5);
      if (ct_eq(q, fq)) return 0.0;
      return b - a * floor(q);
    }
  }
  rt_error("bad scalar function");
  return 0.0;
}

static double smon0(int id, double a)
{
  switch (id) {
  case P_ADD: return a;
  case P_SUB: return -a;
  case P_ABS: return fabs(a);
  case P_FLOOR: {
    double f = floor(a);
    if (f + 1.0 - a <= Q_CT * (fabs(a) > 1.0 ? fabs(a) : 1.0)) f += 1.0;
    return f;
  }
  case P_CEIL: {
    double f = ceil(a);
    if (a - (f - 1.0) <= Q_CT * (fabs(a) > 1.0 ? fabs(a) : 1.0)) f -= 1.0;
    return f;
  }
  case P_NOT: return a == 0.0 ? 1.0 : 0.0;
  case P_MUL: return a > 0.0 ? 1.0 : (a < 0.0 ? -1.0 : 0.0);
  case P_DIV:
    if (a == 0.0) rt_error("DOMAIN ERROR: divide by zero");
    return 1.0 / a;
  case P_POW: return exp(a);
  case P_LOG:
    if (a <= 0.0) rt_error("DOMAIN ERROR: ln of a non-positive number");
    return log(a);
  case P_BINOM: return fact_d(a);
  case P_CIRCLE: return 3.14159265358979323846 * a;
  case P_ROLL:
    if (a != floor(a) || a < 0.0) rt_error("DOMAIN ERROR: roll needs a non-negative integer");
    if (a == 0.0) return rnd_next();
    return floor(rnd_next() * a) + (double)Q_IO;
  }
  rt_error("bad scalar function");
  return 0.0;
}


/* results must stay inside the range every target machine can hold
 * (hexadecimal floating point on VM/370 tops out near 7.2e75) */
static double chk_num(double v)
{
  if (v != v || v > APL_NUM_MAX || v < -APL_NUM_MAX)
    rt_error("DOMAIN ERROR: number out of range");
  return v;
}
static double sdya(int id, double a, double b) { return chk_num(sdya0(id, a, b)); }
static double smon(int id, double a) { return chk_num(smon0(id, a)); }

static int is_scalar_dya(int id) { return id >= P_ADD && id <= P_NOR; }
static int is_scalar_mon(int id)
{
  return id == P_ADD || id == P_SUB || id == P_MUL || id == P_DIV ||
         (id >= P_POW && id <= P_CIRCLE && id != P_NAND && id != P_NOR) ||
         (id >= P_ABS && id <= P_ROLL);
}

static Arr *scalar_mon(int id, Arr *x)
{
  Arr *r;
  int i;
  if (x->type == T_NEST) {
    r = a_new(T_NEST, x->rank, x->shape);
    for (i = 0; i < x->n; i++) {
      Arr *e = a_item(x, i);
      Arr *v = scalar_mon(id, e);
      a_rel(e);
      a_put(r, i, v);
    }
    return a_simplify(r);
  }
  need_num(x, id);
  r = a_new(T_NUM, x->rank, x->shape);
  for (i = 0; i < x->n; i++) r->num[i] = smon(id, x->num[i]);
  return r;
}

static Arr *scalar_dya(int id, Arr *x, Arr *y)
{
  Arr *r;
  int i;
  const int *shp;
  int rank;
  if (x->type == T_NEST || y->type == T_NEST) {
    if (x->rank == 0) { shp = y->shape; rank = y->rank; }
    else if (y->rank == 0) { shp = x->shape; rank = x->rank; }
    else {
      if (!a_same_shape(x, y)) {
        char buf[96];
        sprintf(buf, "shape mismatch: %d / %d", x->n, y->n);
        rt_error(buf);
      }
      shp = x->shape; rank = x->rank;
    }
    r = a_new(T_NEST, rank, shp);
    for (i = 0; i < r->n; i++) {
      Arr *ex = a_item(x, (x->rank == 0) ? 0 : i);
      Arr *ey = a_item(y, (y->rank == 0) ? 0 : i);
      Arr *v = scalar_dya(id, ex, ey);
      a_rel(ex); a_rel(ey);
      a_put(r, i, v);
    }
    return a_simplify(r);
  }
  if (x->type == T_CHR || y->type == T_CHR) {
    if (id != P_EQ && id != P_NE) need_num(x->type == T_CHR ? x : y, id);
  }
  if (x->rank == 0) { shp = y->shape; rank = y->rank; }
  else if (y->rank == 0) { shp = x->shape; rank = x->rank; }
  else {
    if (!a_same_shape(x, y)) {
      char buf[96];
      sprintf(buf, "shape mismatch: %d / %d", x->n, y->n);
      rt_error(buf);
    }
    shp = x->shape; rank = x->rank;
  }
  r = a_new(T_NUM, rank, shp);
  for (i = 0; i < r->n; i++) {
    int xi = (x->rank == 0) ? 0 : i;
    int yi = (y->rank == 0) ? 0 : i;
    if (x->type == T_CHR && y->type == T_CHR)
      r->num[i] = (x->chr[xi] == y->chr[yi]) == (id == P_EQ) ? 1.0 : 0.0;
    else if (x->type == T_CHR || y->type == T_CHR)
      r->num[i] = (id == P_NE) ? 1.0 : 0.0;
    else
      r->num[i] = sdya(id, x->num[xi], y->num[yi]);
  }
  return r;
}

/* ---------- shape helpers ---------- */
static int ivec_arg(Arr *a, int id, int *out, int maxn)
{
  int i;
  need_num(a, id);
  if (a->rank > 1) rt_error_s("DOMAIN ERROR: ", prim_name(id),
                              " needs a vector");
  if (a->n > maxn) rt_error("RANK ERROR");
  for (i = 0; i < a->n; i++) out[i] = as_int(a->num[i], id);
  return a->n;
}

static Arr *shape_of(Arr *x)
{
  Arr *r = a_vec(T_NUM, x->rank);
  int i;
  for (i = 0; i < x->rank; i++) r->num[i] = (double)x->shape[i];
  return r;
}

static Arr *reshape(Arr *left, Arr *x)
{
  int sh[MAXRANK], rank, i, n;
  Arr *r;
  rank = ivec_arg(left, P_RHO, sh, MAXRANK);
  for (i = 0; i < rank; i++)
    if (sh[i] < 0) rt_error("DOMAIN ERROR: rho needs non-negative sizes");
  r = a_like(x, rank, sh);
  n = r->n;
  for (i = 0; i < n; i++) {
    if (x->n == 0) a_fill(r, i);
    else a_copyel(r, i, x, i % x->n);
  }
  return r;
}

static Arr *iota_p(Arr *x)
{
  int n, i;
  Arr *r;
  need_num(x, P_IOTA);
  if (x->n > 1 && x->rank == 1) {
    int sh[MAXRANK], idx[MAXRANK], tot, k, d;
    double dt = 1.0;
    Arr *res;
    if (x->n > MAXRANK) rt_error("RANK ERROR");
    for (d = 0; d < x->n; d++) {
      sh[d] = as_int(x->num[d], P_IOTA);
      if (sh[d] < 0) rt_error("DOMAIN ERROR: iota needs non-negative integers");
      dt *= (double)sh[d]; idx[d] = 0;
    }
    if (dt > (double)APL_MAXELEMS) rt_error("WS FULL");
    tot = (int)dt;
    res = a_new(T_NEST, x->n, sh);
    for (k = 0; k < tot; k++) {
      Arr *c = a_vec(T_NUM, x->n);
      for (d = 0; d < x->n; d++) c->num[d] = (double)(idx[d] + Q_IO);
      a_put(res, k, c);
      for (d = x->n - 1; d >= 0; d--) {
        if (++idx[d] < sh[d]) break;
        idx[d] = 0;
      }
    }
    return res;
  }
  if (x->n != 1) rt_error("DOMAIN ERROR: iota needs a scalar");
  if (x->num[0] != floor(x->num[0]) || x->num[0] < 0.0)
    rt_error("iota requires a non-negative integer");
  n = as_int(x->num[0], P_IOTA);
  r = a_vec(T_NUM, n);
  for (i = 0; i < n; i++) r->num[i] = (double)(Q_IO + i);
  return r;
}

static Arr *ravel_p(Arr *x)
{
  Arr *r = a_vec(x->type, x->n);
  int i;
  for (i = 0; i < x->n; i++) a_copyel(r, i, x, i);
  return r;
}

static Arr *transpose_p(Arr *x)
{
  int sh[MAXRANK], st[MAXRANK], idx[MAXRANK];
  int i, k, rank = x->rank;
  Arr *r;
  if (rank < 2) return a_ref(x);
  /* strides of x */
  st[rank - 1] = 1;
  for (i = rank - 2; i >= 0; i--) st[i] = st[i + 1] * x->shape[i + 1];
  for (i = 0; i < rank; i++) sh[i] = x->shape[rank - 1 - i];
  r = a_like(x, rank, sh);
  for (i = 0; i < rank; i++) idx[i] = 0;
  for (k = 0; k < r->n; k++) {
    int src = 0;
    for (i = 0; i < rank; i++) src += idx[i] * st[rank - 1 - i];
    a_copyel(r, k, x, src);
    for (i = rank - 1; i >= 0; i--) {
      if (++idx[i] < sh[i]) break;
      idx[i] = 0;
    }
  }
  return r;
}

/* take (is_take = 1) and drop (is_take = 0) for any rank */
static Arr *take_drop(Arr *left, Arr *x, int is_take)
{
  int id = is_take ? P_TAKE : P_DROP;
  int cnt[MAXRANK], len[MAXRANK], off[MAXRANK], idx[MAXRANK];
  int xs[MAXRANK], st[MAXRANK];
  int nc, rank, i, k;
  Arr *r;
  need_num(left, id);
  rank = (x->rank == 0) ? 1 : x->rank;
  for (i = 0; i < rank; i++) xs[i] = (x->rank == 0) ? 1 : x->shape[i];
  nc = ivec_arg(left, id, cnt, MAXRANK);
  if (left->rank == 0 && rank == 1) nc = 1;
  if (left->rank == 0 && rank > 1)
    rt_error_s("DOMAIN ERROR: ", prim_name(id),
               " needs one count per axis");
  if (nc != rank)
    rt_error_s("DOMAIN ERROR: ", prim_name(id),
               " needs one count per axis");
  for (i = 0; i < rank; i++) {
    int c = cnt[i];
    if (is_take) {
      len[i] = (c >= 0) ? c : -c;
      off[i] = (c >= 0) ? 0 : xs[i] - len[i];
    } else {
      if (c >= 0) { off[i] = c; len[i] = xs[i] - c; }
      else { off[i] = 0; len[i] = xs[i] + c; }
      if (len[i] < 0) len[i] = 0;
    }
  }
  st[rank - 1] = 1;
  for (i = rank - 2; i >= 0; i--) st[i] = st[i + 1] * xs[i + 1];
  r = a_like(x, rank, len);
  for (i = 0; i < rank; i++) idx[i] = 0;
  for (k = 0; k < r->n; k++) {
    int src = 0, ok = 1;
    for (i = 0; i < rank; i++) {
      int s = idx[i] + off[i];
      if (s < 0 || s >= xs[i]) { ok = 0; break; }
      src += s * st[i];
    }
    if (ok) a_copyel(r, k, x, src); else a_fill(r, k);
    for (i = rank - 1; i >= 0; i--) {
      if (++idx[i] < len[i]) break;
      idx[i] = 0;
    }
  }
  return r;
}

/* ---------- axis helpers ---------- */
static int cur_axis = -1;      /* set by the parser just before a call */

static int axis_pick(const Arr *x, int ax, int id)
{
  int r = (x->rank == 0) ? 1 : x->rank;
  if (ax < 0) return r - 1;
  if (ax >= r) rt_error_s("AXIS ERROR: ", prim_name(id), " axis out of range");
  return ax;
}

static void axis_dims(const Arr *x, int k, int *outer, int *len, int *inner)
{
  int i;
  *outer = 1; *inner = 1;
  if (x->rank == 0) { *len = 1; return; }
  for (i = 0; i < k; i++) *outer *= x->shape[i];
  *len = x->shape[k];
  for (i = k + 1; i < x->rank; i++) *inner *= x->shape[i];
}

static Arr *reverse_p(Arr *x, int ax)
{
  Arr *r = a_like(x, x->rank, x->shape);
  int k, outer, len, inner, o, j, i;
  k = axis_pick(x, ax, P_REVERSE);
  axis_dims(x, k, &outer, &len, &inner);
  for (o = 0; o < outer; o++)
    for (j = 0; j < len; j++)
      for (i = 0; i < inner; i++)
        a_copyel(r, (o * len + j) * inner + i, x,
                 (o * len + (len - 1 - j)) * inner + i);
  return r;
}

static Arr *rotate_p(Arr *left, Arr *x, int ax)
{
  Arr *r = a_like(x, x->rank, x->shape);
  int k, outer, len, inner, o, j, i;
  need_num(left, P_ROTATE);
  k = axis_pick(x, ax, P_ROTATE);
  axis_dims(x, k, &outer, &len, &inner);
  if (left->n != 1 && left->n != outer * inner)
    rt_error("DOMAIN ERROR: rotate count shape does not match");
  for (o = 0; o < outer; o++)
    for (i = 0; i < inner; i++) {
      int c = as_int(left->num[left->n == 1 ? 0 : o * inner + i], P_ROTATE);
      for (j = 0; j < len; j++) {
        int s = (len > 0) ? (int)(((long)j + c) % len) : 0;
        if (s < 0) s += len;
        a_copyel(r, (o * len + j) * inner + i, x, (o * len + s) * inner + i);
      }
    }
  return r;
}

/* general permutation / diagonal transpose: left is the axis list */
static Arr *transpose_g(Arr *left, Arr *x)
{
  int perm[MAXRANK], sh[MAXRANK] = {0}, st[MAXRANK], idx[MAXRANK];
  int rank = x->rank, i, j, k, nr = 0;
  Arr *r;
  need_num(left, P_TRANSPOSE);
  if (left->n != rank)
    rt_error("LENGTH ERROR: transpose needs one axis number per axis");
  for (i = 0; i < rank; i++) {
    perm[i] = as_int(left->num[i], P_TRANSPOSE) - Q_IO;
    if (perm[i] < 0 || perm[i] >= rank) rt_error("DOMAIN ERROR: transpose axis out of range");
    if (perm[i] + 1 > nr) nr = perm[i] + 1;
  }
  for (j = 0; j < nr; j++) {
    int seen = 0;
    sh[j] = 0;
    for (i = 0; i < rank; i++)
      if (perm[i] == j) {
        if (!seen || x->shape[i] < sh[j]) sh[j] = x->shape[i];
        seen = 1;
      }
    if (!seen) rt_error("DOMAIN ERROR: transpose axes must cover 1..n");
  }
  if (rank > 0) {
    st[rank - 1] = 1;
    for (i = rank - 2; i >= 0; i--) st[i] = st[i + 1] * x->shape[i + 1];
  }
  r = a_like(x, nr, sh);
  for (j = 0; j < nr; j++) idx[j] = 0;
  for (k = 0; k < r->n; k++) {
    int src = 0;
    for (i = 0; i < rank; i++) src += idx[perm[i]] * st[i];
    a_copyel(r, k, x, src);
    for (j = nr - 1; j >= 0; j--) {
      if (++idx[j] < sh[j]) break;
      idx[j] = 0;
    }
  }
  return r;
}

static Arr *cat_core(Arr *x, Arr *y, int ax)
{
  int R, rx = x->rank, ry = y->rank, i, k, o, j, in;
  int sx[MAXRANK], sy[MAXRANK], sr[MAXRANK];
  int outer, inner, lx, ly;
  Arr *r;
  R = rx > ry ? rx : ry;
  if (R == 0) R = 1;
  if (rx != 0 && ry != 0 && rx != ry && rx != ry + 1 && ry != rx + 1)
    rt_error("RANK ERROR: cat");
  k = (ax < 0) ? R - 1 : ax;
  if (k >= R) rt_error("AXIS ERROR: cat axis out of range");
  /* shape of the longer one fixes the frame */
  {
    const Arr *big = (rx >= ry) ? x : y;
    for (i = 0; i < R; i++) {
      if (big->rank == R) sr[i] = big->shape[i];
      else sr[i] = (i < k) ? big->shape[i] : (i == k ? 1 : big->shape[i - 1]);
    }
    if (big->rank == 0) sr[0] = 1;
  }
  for (i = 0; i < R; i++) {
    if (rx == R) sx[i] = x->shape[i];
    else if (rx == 0) sx[i] = (i == k) ? 1 : sr[i];
    else sx[i] = (i < k) ? x->shape[i] : (i == k ? 1 : x->shape[i - 1]);
    if (ry == R) sy[i] = y->shape[i];
    else if (ry == 0) sy[i] = (i == k) ? 1 : sr[i];
    else sy[i] = (i < k) ? y->shape[i] : (i == k ? 1 : y->shape[i - 1]);
  }
  for (i = 0; i < R; i++) {
    if (i != k && sx[i] != sy[i]) rt_error("LENGTH ERROR: cat dimensions differ");
    sr[i] = sx[i];
  }
  sr[k] = sx[k] + sy[k];
  outer = 1; inner = 1;
  for (i = 0; i < k; i++) outer *= sr[i];
  for (i = k + 1; i < R; i++) inner *= sr[i];
  lx = sx[k]; ly = sy[k];
  r = a_like(x, R, sr);
  for (o = 0; o < outer; o++) {
    for (j = 0; j < lx; j++)
      for (in = 0; in < inner; in++)
        a_copyel(r, (o * (lx + ly) + j) * inner + in, x,
                 rx == 0 ? 0 : (o * lx + j) * inner + in);
    for (j = 0; j < ly; j++)
      for (in = 0; in < inner; in++)
        a_copyel(r, (o * (lx + ly) + lx + j) * inner + in, y,
                 ry == 0 ? 0 : (o * ly + j) * inner + in);
  }
  return r;
}

static Arr *cat_p(Arr *x, Arr *y, int ax)
{
  Arr *a, *b, *r;
  if (x->type == y->type) return cat_core(x, y, ax);
  a = a_to_nest(x);
  b = a_to_nest(y);
  r = cat_core(a, b, ax);
  a_rel(a); a_rel(b);
  return r;
}

/* replicate (/) along an axis; counts may repeat items or insert fills */
static Arr *compress_p(Arr *b, Arr *x, int ax)
{
  int k, outer, len, inner, o, j, i, c, total = 0, sh[MAXRANK], rank, pos;
  Arr *r;
  need_num(b, P_COMPRESS);
  if (b->rank > 1) rt_error("RANK ERROR: compress needs a vector");
  rank = (x->rank == 0) ? 1 : x->rank;
  k = axis_pick(x, ax, P_COMPRESS);
  axis_dims(x, k, &outer, &len, &inner);
  if (b->n != 1 && b->n != len) {
    char buf[96];
    sprintf(buf, "LENGTH ERROR: compress %d / %d", b->n, len);
    rt_error(buf);
  }
  for (j = 0; j < len; j++) {
    c = as_int(b->num[b->n == 1 ? 0 : j], P_COMPRESS);
    total += (c < 0 ? -c : c);
  }
  for (i = 0; i < rank; i++) sh[i] = (x->rank == 0) ? 1 : x->shape[i];
  sh[k] = total;
  r = a_like(x, rank, sh);
  for (o = 0; o < outer; o++) {
    pos = 0;
    for (j = 0; j < len; j++) {
      int t;
      c = as_int(b->num[b->n == 1 ? 0 : j], P_COMPRESS);
      for (t = 0; t < (c < 0 ? -c : c); t++, pos++)
        for (i = 0; i < inner; i++) {
          int d = (o * total + pos) * inner + i;
          if (c > 0) a_copyel(r, d, x, (o * len + j) * inner + i);
          else a_fill(r, d);
        }
    }
  }
  return r;
}

/* expand (\): zeros in the left argument insert fill items */
static Arr *expand_p(Arr *b, Arr *x, int ax)
{
  int k, outer, len, inner, o, j, i, ones = 0, sh[MAXRANK], rank, pos;
  Arr *r;
  need_num(b, P_EXPAND);
  if (b->rank > 1) rt_error("RANK ERROR: expand needs a vector");
  rank = (x->rank == 0) ? 1 : x->rank;
  k = axis_pick(x, ax, P_EXPAND);
  axis_dims(x, k, &outer, &len, &inner);
  for (j = 0; j < b->n; j++) {
    if (b->num[j] != 0.0 && b->num[j] != 1.0)
      rt_error("DOMAIN ERROR: expand needs 0s and 1s");
    if (b->num[j] == 1.0) ones++;
  }
  if (ones != len && !(x->rank == 0 && len == 1))
    rt_error("LENGTH ERROR: expand count of 1s differs from axis length");
  if (x->rank == 0 && ones > 1)
    rt_error("LENGTH ERROR: expand count of 1s differs from axis length");
  for (i = 0; i < rank; i++) sh[i] = (x->rank == 0) ? 1 : x->shape[i];
  sh[k] = b->n;
  r = a_like(x, rank, sh);
  for (o = 0; o < outer; o++) {
    pos = 0;
    for (j = 0; j < b->n; j++) {
      int one = (b->num[j] == 1.0);
      for (i = 0; i < inner; i++) {
        int d = (o * b->n + j) * inner + i;
        if (one) a_copyel(r, d, x, (o * len + pos) * inner + i);
        else a_fill(r, d);
      }
      if (one) pos++;
    }
  }
  return r;
}

/* ---------- grade, search and set functions ---------- */
static const Arr *g_sort;
static int g_cell;
static const char *g_coll;     /* collating sequence for character grade */
static int g_collen;
static int g_desc;

static int coll_pos(char c)
{
  int i;
  for (i = 0; i < g_collen; i++) if (g_coll[i] == c) return i;
  return g_collen;
}

static int cell_cmp(int a, int b)
{
  int i;
  const Arr *x = g_sort;
  for (i = 0; i < g_cell; i++) {
    int p = a * g_cell + i, q = b * g_cell + i, c;
    if (x->type == T_CHR) {
      if (g_coll) {
        int u = coll_pos(x->chr[p]), v = coll_pos(x->chr[q]);
        c = (u > v) - (u < v);
      } else {
        unsigned char u = (unsigned char)x->chr[p], v = (unsigned char)x->chr[q];
        c = (u > v) - (u < v);
      }
    } else {
      c = (x->num[p] > x->num[q]) - (x->num[p] < x->num[q]);
    }
    if (c) return c;
  }
  return 0;
}

/* stable merge sort of an index array */
static void msort(int *a, int *tmp, int n)
{
  int mid, i, j, k;
  if (n < 2) return;
  mid = n / 2;
  msort(a, tmp, mid);
  msort(a + mid, tmp, n - mid);
  i = 0; j = mid; k = 0;
  while (i < mid && j < n) {
    int c = cell_cmp(a[i], a[j]);
    if (g_desc) c = -c;
    if (c <= 0) tmp[k++] = a[i++]; else tmp[k++] = a[j++];
  }
  while (i < mid) tmp[k++] = a[i++];
  while (j < n) tmp[k++] = a[j++];
  for (i = 0; i < n; i++) a[i] = tmp[i];
}

static Arr *grade_p(Arr *coll, Arr *x, int desc)
{
  int n, i, *idx, *tmp;
  Arr *r;
  int id = desc ? P_GRADEDN : P_GRADEUP;
  if (x->type == T_NEST) rt_error_s("DOMAIN ERROR: ", prim_name(id), " does not accept a nested value");
  if (x->rank == 0) rt_error_s("RANK ERROR: ", prim_name(id), " needs an array");
  n = x->shape[0];
  g_sort = x;
  g_cell = (n > 0) ? x->n / n : 0;
  g_coll = NULL; g_collen = 0;
  if (coll) {
    if (coll->type != T_CHR || x->type != T_CHR)
      rt_error("DOMAIN ERROR: collating sequence must be text");
    g_coll = coll->chr; g_collen = coll->n;
  }
  g_desc = desc;
  idx = (int *)xmalloc(sizeof(int) * (size_t)(n ? n : 1));
  tmp = (int *)xmalloc(sizeof(int) * (size_t)(n ? n : 1));
  for (i = 0; i < n; i++) idx[i] = i;
  msort(idx, tmp, n);
  r = a_vec(T_NUM, n);
  for (i = 0; i < n; i++) r->num[i] = (double)(idx[i] + Q_IO);
  free(idx); free(tmp);
  g_coll = NULL;
  return r;
}

static int item_in(const Arr *e, const Arr *y)
{
  int j, found = 0;
  for (j = 0; j < y->n && !found; j++) {
    Arr *f = a_item(y, j);
    found = a_match(e, f);
    a_rel(f);
  }
  return found;
}

static Arr *vec_of(Arr *x)
{
  if (x->rank == 1) return a_ref(x);
  return ravel_p(x);
}

static Arr *indexof_p(Arr *x, Arr *y)
{
  Arr *r, *xv;
  int i, j;
  if (x->rank > 1) rt_error("RANK ERROR: indexof needs a vector on the left");
  xv = vec_of(x);
  r = a_new(T_NUM, y->rank, y->shape);
  for (i = 0; i < y->n; i++) {
    Arr *e = a_item(y, i);
    int pos = xv->n;
    for (j = 0; j < xv->n; j++) {
      Arr *f = a_item(xv, j);
      int m = a_match(e, f);
      a_rel(f);
      if (m) { pos = j; break; }
    }
    a_rel(e);
    r->num[i] = (double)(pos + Q_IO);
  }
  a_rel(xv);
  return r;
}

/* keep items of x for which keep[i] is set */
static Arr *select_items(const Arr *x, const int *keep)
{
  int i, c = 0, k = 0;
  Arr *r;
  for (i = 0; i < x->n; i++) c += keep[i];
  r = a_vec(x->type, c);
  for (i = 0; i < x->n; i++)
    if (keep[i]) a_copyel(r, k++, x, i);
  return r;
}

static Arr *nub_keep(Arr *xv, int **keepp)
{
  int i, j, *keep = (int *)xmalloc(sizeof(int) * (size_t)(xv->n ? xv->n : 1));
  for (i = 0; i < xv->n; i++) {
    Arr *e = a_item(xv, i);
    keep[i] = 1;
    for (j = 0; j < i && keep[i]; j++) {
      Arr *f = a_item(xv, j);
      if (a_match(e, f)) keep[i] = 0;
      a_rel(f);
    }
    a_rel(e);
  }
  *keepp = keep;
  return xv;
}

static Arr *unique_p(Arr *x)
{
  Arr *xv = vec_of(x), *r;
  int *keep;
  if (x->rank > 1) rt_error("RANK ERROR: unique needs a vector");
  nub_keep(xv, &keep);
  r = select_items(xv, keep);
  free(keep); a_rel(xv);
  return r;
}

static Arr *nubsieve_p(Arr *x)
{
  Arr *xv = vec_of(x), *r;
  int *keep, i;
  if (x->rank > 1) rt_error("RANK ERROR: nubsieve needs a vector");
  nub_keep(xv, &keep);
  r = a_vec(T_NUM, xv->n);
  for (i = 0; i < xv->n; i++) r->num[i] = (double)keep[i];
  free(keep); a_rel(xv);
  return r;
}

static Arr *without_p(Arr *x, Arr *y)
{
  Arr *xv = vec_of(x), *r;
  int *keep, i;
  if (x->rank > 1) rt_error("RANK ERROR: without needs a vector");
  keep = (int *)xmalloc(sizeof(int) * (size_t)(xv->n ? xv->n : 1));
  for (i = 0; i < xv->n; i++) {
    Arr *e = a_item(xv, i);
    keep[i] = !item_in(e, y);
    a_rel(e);
  }
  r = select_items(xv, keep);
  free(keep); a_rel(xv);
  return r;
}

static Arr *intersect_p(Arr *x, Arr *y)
{
  Arr *xv = vec_of(x), *r;
  int *keep, i;
  if (x->rank > 1) rt_error("RANK ERROR: intersect needs vectors");
  keep = (int *)xmalloc(sizeof(int) * (size_t)(xv->n ? xv->n : 1));
  for (i = 0; i < xv->n; i++) {
    Arr *e = a_item(xv, i);
    keep[i] = item_in(e, y);
    a_rel(e);
  }
  r = select_items(xv, keep);
  free(keep); a_rel(xv);
  return r;
}

static Arr *union_p(Arr *x, Arr *y)
{
  Arr *rest = without_p(y, x), *xv = vec_of(x), *r;
  r = cat_p(xv, rest, -1);
  a_rel(rest); a_rel(xv);
  return r;
}

/* find: marks where x occurs as a sub-array of y */
static Arr *find_p(Arr *x, Arr *y)
{
  int sx[MAXRANK], i, k, j, rank = y->rank;
  int idx[MAXRANK], st[MAXRANK];
  Arr *r;
  if (rank == 0) {
    Arr *e = a_item(y, 0);
    r = a_scalar(a_match(x, e) ? 1.0 : 0.0);
    a_rel(e);
    return r;
  }
  if (x->rank > rank) rt_error("RANK ERROR: find");
  for (i = 0; i < rank; i++)
    sx[i] = (i < rank - x->rank) ? 1 : x->shape[i - (rank - x->rank)];
  st[rank - 1] = 1;
  for (i = rank - 2; i >= 0; i--) st[i] = st[i + 1] * y->shape[i + 1];
  r = a_new(T_NUM, rank, y->shape);
  for (i = 0; i < rank; i++) idx[i] = 0;
  for (k = 0; k < r->n; k++) {
    int ok = 1, m = 0;
    for (i = 0; i < rank; i++) if (idx[i] + sx[i] > y->shape[i]) ok = 0;
    if (ok) {
      int sub[MAXRANK];
      for (i = 0; i < rank; i++) sub[i] = 0;
      for (m = 0; m < x->n && ok; m++) {
        int src = 0;
        Arr *e, *f;
        for (i = 0; i < rank; i++) src += (idx[i] + sub[i]) * st[i];
        e = a_item(x, m); f = a_item(y, src);
        if (!a_match(e, f)) ok = 0;
        a_rel(e); a_rel(f);
        for (i = rank - 1; i >= 0; i--) {
          if (++sub[i] < sx[i]) break;
          sub[i] = 0;
        }
      }
    }
    r->num[k] = (ok && x->n > 0) ? 1.0 : 0.0;
    for (j = rank - 1; j >= 0; j--) {
      if (++idx[j] < y->shape[j]) break;
      idx[j] = 0;
    }
  }
  return r;
}

/* where: indices of the 1s (or repeat counts) */
static Arr *where_p(Arr *x)
{
  int i, j, total = 0, k = 0;
  Arr *r;
  need_num(x, P_WHERE);
  for (i = 0; i < x->n; i++) {
    int c = as_int(x->num[i], P_WHERE);
    if (c < 0) rt_error("DOMAIN ERROR: where needs non-negative counts");
    total += c;
  }
  if (x->rank <= 1) {
    r = a_vec(T_NUM, total);
    for (i = 0; i < x->n; i++)
      for (j = 0; j < (int)x->num[i]; j++) r->num[k++] = (double)(i + Q_IO);
    return r;
  }
  {
    int idx[MAXRANK];
    r = a_vec(T_NEST, total);
    for (i = 0; i < x->rank; i++) idx[i] = 0;
    for (i = 0; i < x->n; i++) {
      for (j = 0; j < (int)x->num[i]; j++) {
        Arr *c = a_vec(T_NUM, x->rank);
        int m;
        for (m = 0; m < x->rank; m++) c->num[m] = (double)(idx[m] + Q_IO);
        a_put(r, k++, c);
      }
      for (j = x->rank - 1; j >= 0; j--) {
        if (++idx[j] < x->shape[j]) break;
        idx[j] = 0;
      }
    }
    return r;
  }
}

/* split: a nested array of the rows along the last axis */
static Arr *split_p(Arr *x)
{
  int len, rows, i, j;
  Arr *r;
  if (x->rank == 0) return a_ref(x);
  len = x->shape[x->rank - 1];
  rows = (len > 0) ? x->n / len : 1;
  if (len == 0) {
    rows = 1;
    for (i = 0; i < x->rank - 1; i++) rows *= x->shape[i];
  }
  r = a_new(T_NEST, x->rank - 1, x->shape);
  for (i = 0; i < rows; i++) {
    Arr *row = a_like(x, 1, &len);
    for (j = 0; j < len; j++) a_copyel(row, j, x, i * len + j);
    a_put(r, i, row);
  }
  return r;
}


/* ---------- bracket indexing: x[i;j;...] (an elided index is NULL) ---------- */
static int ix_prepare(Arr *x, Arr **ix, int n, int **pos, int *cnt, int *sh)
{
  int a, rank = 0, i;
  if (x->rank == 0) rt_error("RANK ERROR: cannot index a scalar");
  if (n != x->rank) rt_error("RANK ERROR: index count differs from array rank");
  for (a = 0; a < n; a++) {
    if (!ix[a]) {
      cnt[a] = x->shape[a];
      pos[a] = (int *)xmalloc(sizeof(int) * (size_t)(cnt[a] ? cnt[a] : 1));
      for (i = 0; i < cnt[a]; i++) pos[a][i] = i;
      if (rank + 1 > MAXRANK) rt_error("RANK ERROR");
      sh[rank++] = cnt[a];
    } else {
      need_num(ix[a], P_INDEX);
      cnt[a] = ix[a]->n;
      pos[a] = (int *)xmalloc(sizeof(int) * (size_t)(cnt[a] ? cnt[a] : 1));
      for (i = 0; i < cnt[a]; i++) {
        int s = as_int(ix[a]->num[i], P_INDEX) - Q_IO;
        if (s < 0 || s >= x->shape[a]) {
          int b;
          for (b = 0; b <= a; b++) free(pos[b]);
          rt_error("INDEX ERROR: subscript out of range");
        }
        pos[a][i] = s;
      }
      if (rank + ix[a]->rank > MAXRANK) rt_error("RANK ERROR");
      for (i = 0; i < ix[a]->rank; i++) sh[rank++] = ix[a]->shape[i];
    }
  }
  return rank;
}

/* linear source offset for the multi-index in odo[] */
static int ix_offset(const Arr *x, int **pos, const int *odo)
{
  int a, off = 0, st = 1;
  for (a = x->rank - 1; a >= 0; a--) {
    off += pos[a][odo[a]] * st;
    st *= x->shape[a];
  }
  return off;
}

static int ix_next(int n, const int *cnt, int *odo)
{
  int a;
  for (a = n - 1; a >= 0; a--) {
    if (++odo[a] < cnt[a]) return 1;
    odo[a] = 0;
  }
  return 0;
}

static Arr *select_p(Arr *x, Arr **ix, int n)
{
  int *pos[MAXRANK], cnt[MAXRANK], odo[MAXRANK], sh[MAXRANK];
  int rank, a, k, total = 1;
  Arr *r;
  rank = ix_prepare(x, ix, n, pos, cnt, sh);
  r = a_like(x, rank, sh);
  for (a = 0; a < n; a++) { odo[a] = 0; total *= cnt[a]; }
  for (k = 0; k < total; k++) {
    a_copyel(r, k, x, ix_offset(x, pos, odo));
    ix_next(n, cnt, odo);
  }
  for (a = 0; a < n; a++) free(pos[a]);
  return a_simplify(r);
}

/* returns the updated array (a new reference); consumes nothing */
static Arr *assign_index_p(Arr *x, Arr **ix, int n, Arr *val)
{
  int *pos[MAXRANK], cnt[MAXRANK], odo[MAXRANK], sh[MAXRANK];
  int rank, a, k, total = 1;
  Arr *r, *v, *base;
  rank = ix_prepare(x, ix, n, pos, cnt, sh);
  (void)rank;
  for (a = 0; a < n; a++) { odo[a] = 0; total *= cnt[a]; }
  if (val->n != 1 && val->n != total) {
    char buf[96];
    for (a = 0; a < n; a++) free(pos[a]);
    sprintf(buf, "LENGTH ERROR: indexed assignment %d / %d", val->n, total);
    rt_error(buf);
  }
  if (x->type == val->type) { base = a_ref(x); v = a_ref(val); }
  else { base = a_to_nest(x); v = a_to_nest(val); }
  r = a_like(base, x->rank, x->shape);
  for (k = 0; k < x->n; k++) a_copyel(r, k, base, k);
  a_rel(base);
  for (k = 0; k < total; k++) {
    a_copyel(r, ix_offset(x, pos, odo), v, v->n == 1 ? 0 : k);
    ix_next(n, cnt, odo);
  }
  a_rel(v);
  for (a = 0; a < n; a++) free(pos[a]);
  return a_simplify(r);
}

static Arr *index_p(Arr *x, Arr *ix)
{
  int i, inner;
  Arr *r;
  need_num(ix, P_INDEX);
  if (x->rank == 0) rt_error("DOMAIN ERROR: index needs an array");
  if (x->rank == 1) {
    /* gather */
    r = a_new(x->type, ix->rank, ix->shape);
    for (i = 0; i < ix->n; i++) {
      int s = as_int(ix->num[i], P_INDEX) - Q_IO;
      if (s < 0 || s >= x->n) {
        a_rel(r);
        rt_error("index: subscript out of range");
      }
      a_copyel(r, i, x, s);
    }
    return r;
  }
  if (ix->rank == 0 || (ix->rank == 1 && ix->n == 1)) {
    int s = as_int(ix->num[0], P_INDEX) - Q_IO;
    if (s < 0 || s >= x->shape[0])
      rt_error("index: subscript out of range");
    inner = x->n / (x->shape[0] ? x->shape[0] : 1);
    r = a_like(x, x->rank - 1, x->shape + 1);
    for (i = 0; i < inner; i++) a_copyel(r, i, x, s * inner + i);
    return r;
  }
  if (ix->rank == 1 && ix->n == x->rank) {
    int off = 0, st = 1;
    for (i = x->rank - 1; i >= 0; i--) {
      int s = as_int(ix->num[i], P_INDEX) - Q_IO;
      if (s < 0 || s >= x->shape[i])
        rt_error("index: subscript out of range");
      off += s * st;
      st *= x->shape[i];
    }
    r = a_like(x, 0, NULL);
    a_copyel(r, 0, x, off);
    return r;
  }
  rt_error("index: bad subscript");
  return NULL;
}

static Arr *matmul_p(Arr *x, Arr *y)
{
  int sh[MAXRANK], rank, k, m, n, i, j, l;
  Arr *r;
  need_num(x, P_MATMUL);
  need_num(y, P_MATMUL);
  if (x->rank < 1 || y->rank < 1)
    rt_error("DOMAIN ERROR: matmul needs arrays, not scalars");
  k = x->shape[x->rank - 1];
  if (k != y->shape[0]) {
    char buf[96];
    sprintf(buf, "matmul: inner dimensions differ: %d / %d", k, y->shape[0]);
    rt_error(buf);
  }
  rank = x->rank - 1 + y->rank - 1;
  if (rank > MAXRANK) rt_error("RANK ERROR");
  for (i = 0; i < x->rank - 1; i++) sh[i] = x->shape[i];
  for (j = 1; j < y->rank; j++) sh[x->rank - 1 + j - 1] = y->shape[j];
  r = a_new(T_NUM, rank, sh);
  m = (k > 0) ? x->n / k : 0;
  if (k == 0) {
    m = 1;
    for (i = 0; i < x->rank - 1; i++) m *= x->shape[i];
  }
  n = (k > 0) ? y->n / k : 1;
  if (k == 0) {
    n = 1;
    for (j = 1; j < y->rank; j++) n *= y->shape[j];
  }
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++) {
      double s = 0.0;
      for (l = 0; l < k; l++) s += x->num[i * k + l] * y->num[l * n + j];
      r->num[i * n + j] = s;
    }
  return r;
}


/* ---------- format and execute ---------- */
static Arr *exec_text(const char *s);

static Arr *blk_to_chr(Blk *b)
{
  Arr *r;
  int i, sh[2], w = b->w;
  if ((double)b->h * (double)b->w > (double)APL_MAXELEMS) {
    blk_free(b);
    rt_error("WS FULL");
  }
  if (b->h == 1) {
    while (w > 0 && b->ln[0][w - 1] == ' ') w--;
    r = a_vec(T_CHR, w);
    memcpy(r->chr, b->ln[0], (size_t)w);
    return r;
  }
  sh[0] = b->h; sh[1] = b->w;
  r = a_new(T_CHR, 2, sh);
  for (i = 0; i < b->h; i++) memcpy(r->chr + i * b->w, b->ln[i], (size_t)b->w);
  return r;
}

static Arr *format_mon(Arr *x)
{
  Blk *b;
  Arr *r;
  if (x->type == T_CHR) return a_ref(x);
  b = render(x);
  r = blk_to_chr(b);
  blk_free(b);
  return r;
}

/* A format B : A is precision, or (width precision) for every column,
 * or one (width precision) pair per column */
static Arr *format_dya(Arr *left, Arr *x)
{
  int cols, rows, j, i, npair, sh[2];
  int *wd, *pr, *cw;
  char **cell;
  Arr *r;
  int total = 0, row, c;
  need_num(left, P_FORMAT);
  need_num(x, P_FORMAT);
  if (x->rank > 2) rt_error("RANK ERROR: format");
  if (x->rank == 2) { rows = x->shape[0]; cols = x->shape[1]; }
  else { rows = 1; cols = x->n; }
  if (left->rank > 1) rt_error("RANK ERROR: format");
  if (left->n == 1) npair = 0;
  else if (left->n == 2) npair = 1;
  else if (left->n == 2 * cols) npair = cols;
  else rt_error("LENGTH ERROR: format left argument");
  wd = (int *)xmalloc(sizeof(int) * (size_t)(cols ? cols : 1));
  pr = (int *)xmalloc(sizeof(int) * (size_t)(cols ? cols : 1));
  cw = (int *)xmalloc(sizeof(int) * (size_t)(cols ? cols : 1));
  trk_add(wd, 0); trk_add(pr, 0); trk_add(cw, 0);
  for (j = 0; j < cols; j++) {
    if (npair == 0) { wd[j] = 0; pr[j] = as_int(left->num[0], P_FORMAT); }
    else if (npair == 1) { wd[j] = as_int(left->num[0], P_FORMAT); pr[j] = as_int(left->num[1], P_FORMAT); }
    else { wd[j] = as_int(left->num[2 * j], P_FORMAT); pr[j] = as_int(left->num[2 * j + 1], P_FORMAT); }
    if (wd[j] < 0) rt_error("DOMAIN ERROR: format width");
    cw[j] = wd[j];
  }
  cell = (char **)xmalloc(sizeof(char *) * (size_t)(rows * cols + 1));
  for (i = 0; i < rows; i++)
    for (j = 0; j < cols; j++) {
      char buf[128];
      double v = x->num[i * cols + j];
      if (pr[j] >= 0) sprintf(buf, "%.*f", pr[j] > 40 ? 40 : pr[j], v);
      else {
        char *e;
        sprintf(buf, "%.*e", (-pr[j] > 40 ? 40 : -pr[j]) - 1, v);
        e = strchr(buf, 'e');
        if (e) *e = 'E';
      }
      cell[i * cols + j] = xstrdup(buf);
      if (wd[j] == 0 && (int)strlen(buf) + 1 > cw[j]) cw[j] = (int)strlen(buf) + 1;
    }
  for (j = 0; j < cols; j++) total += cw[j];
  /* width 0 columns get one leading blank except the first column */
  if (cols > 0 && wd[0] == 0) { total -= 1; cw[0] -= 1; }
  sh[0] = rows; sh[1] = total;
  r = a_new(T_CHR, 2, sh);
  memset(r->chr, ' ', (size_t)(rows * total));
  for (i = 0; i < rows; i++) {
    c = 0;
    for (j = 0; j < cols; j++) {
      int len = (int)strlen(cell[i * cols + j]);
      if (len > cw[j]) {
        for (row = 0; row < cw[j]; row++) r->chr[i * total + c + row] = '*';
      } else {
        memcpy(r->chr + i * total + c + (cw[j] - len), cell[i * cols + j], (size_t)len);
      }
      c += cw[j];
      free(cell[i * cols + j]);
    }
  }
  trk_del(cw); trk_del(pr); trk_del(wd);
  free(cell); free(wd); free(pr); free(cw);
  if (rows == 1 && x->rank <= 1) {
    Arr *v = a_vec(T_CHR, total);
    memcpy(v->chr, r->chr, (size_t)total);
    a_rel(r);
    return v;
  }
  return r;
}

static Arr *execute_p(Arr *x)
{
  Arr *r;
  char *s;
  if (x->type != T_CHR || x->rank > 1)
    rt_error("DOMAIN ERROR: execute needs a text vector");
  s = xstrndup(x->chr, x->n);
  trk_add(s, 0);
  r = exec_text(s);
  trk_del(s);
  free(s);
  if (!r) r = a_vec(T_NUM, 0);
  return r;
}


/* ---------- encode, decode, matrix divide, partition ---------- */
static Arr *encode_p(Arr *a, Arr *b)
{
  int na, nb, i, k, sh[MAXRANK] = {0}, rank = 0;
  Arr *r;
  need_num(a, P_ENCODE); need_num(b, P_ENCODE);
  if (a->rank > 1) rt_error("RANK ERROR: encode radix must be a vector");
  na = a->n; nb = b->n;
  if (a->rank == 1) sh[rank++] = na;
  for (i = 0; i < b->rank; i++) {
    if (rank >= MAXRANK) rt_error("RANK ERROR");
    sh[rank++] = b->shape[i];
  }
  if (a->rank == 0) na = 1;
  r = a_new(T_NUM, rank, sh);
  for (k = 0; k < nb; k++) {
    double v = b->num[k];
    for (i = na - 1; i >= 0; i--) {
      double rad = a->num[i], d;
      if (rad == 0.0) { d = v; v = 0.0; }
      else {
        d = v - rad * floor(v / rad);
        v = (v - d) / rad;
      }
      r->num[i * nb + k] = d;
    }
  }
  return r;
}

static Arr *decode_p(Arr *a, Arr *b)
{
  int n, cols, i, j, sh[MAXRANK] = {0}, rank = 0;
  Arr *r;
  double *w;
  need_num(a, P_DECODE); need_num(b, P_DECODE);
  if (a->rank > 1) rt_error("RANK ERROR: decode radix must be a vector");
  if (b->rank > 2) rt_error("RANK ERROR: decode");
  if (b->rank == 0) { n = (a->rank == 0) ? 1 : a->n; cols = 1; }
  else { n = b->shape[0]; cols = b->n / (n ? n : 1); if (n == 0) cols = 1; }
  if (a->rank == 1 && a->n != n && a->n != 1 && b->rank != 0)
    rt_error("LENGTH ERROR: decode");
  if (b->rank == 0 && a->rank == 1) n = a->n;
  w = (double *)xmalloc(sizeof(double) * (size_t)(n ? n : 1));
  if (n > 0) w[n - 1] = 1.0;
  for (i = n - 2; i >= 0; i--) {
    double rad = (a->rank == 0 || a->n == 1) ? a->num[0] : a->num[i + 1];
    w[i] = w[i + 1] * rad;
  }
  if (b->rank == 2) sh[rank++] = cols;
  r = a_new(T_NUM, rank, sh);
  for (j = 0; j < cols; j++) {
    double s = 0.0;
    for (i = 0; i < n; i++) {
      double v = (b->rank == 0) ? b->num[0] : b->num[i * cols + j];
      s += w[i] * v;
    }
    r->num[j] = s;
  }
  free(w);
  return r;
}

/* solve A x = B (A is m x n, B is m x k) by Gaussian elimination on the
 * normal equations when A is not square; monadic: inverse of A */
static Arr *domino_p(Arr *bl, Arr *a)
{
  int m, n, k, i, j, l, sh[2] = {0, 0}, rank, vec = 0;
  double *M, *R, *AtA, *AtB;
  Arr *r;
  need_num(a, P_DOMINO);
  if (a->rank > 2) rt_error("RANK ERROR: domino");
  if (a->rank == 2) { m = a->shape[0]; n = a->shape[1]; }
  else { m = a->n; n = 1; }
  if (a->rank == 0) { m = 1; n = 1; }
  if (bl) {
    need_num(bl, P_DOMINO);
    if (bl->rank > 2) rt_error("RANK ERROR: domino");
    if (bl->rank == 2) k = bl->shape[1]; else { k = 1; vec = 1; }
    if ((bl->rank == 0 ? 1 : bl->shape[0]) != m)
      rt_error("LENGTH ERROR: domino row counts differ");
  } else k = m;
  if (m < n) rt_error("DOMAIN ERROR: domino needs at least as many rows as columns");
  /* square: eliminate A directly; otherwise solve (A'A) X = A'B */
  AtA = (double *)xmalloc(sizeof(double) * (size_t)(n * n));
  AtB = (double *)xmalloc(sizeof(double) * (size_t)(n * (k ? k : 1)));
  if (m == n) {
    for (i = 0; i < n * n; i++) AtA[i] = a->num[i];
    for (i = 0; i < n; i++)
      for (j = 0; j < k; j++)
        AtB[i * k + j] = bl ? bl->num[i * k + j] : (i == j ? 1.0 : 0.0);
  } else {
    for (i = 0; i < n; i++)
      for (j = 0; j < n; j++) {
        double s = 0.0;
        for (l = 0; l < m; l++) s += a->num[l * n + i] * a->num[l * n + j];
        AtA[i * n + j] = s;
      }
    for (i = 0; i < n; i++)
      for (j = 0; j < k; j++) {
        double s = 0.0;
        for (l = 0; l < m; l++) {
          double bv = bl ? bl->num[l * k + j] : (l == j ? 1.0 : 0.0);
          s += a->num[l * n + i] * bv;
        }
        AtB[i * k + j] = s;
      }
  }
  M = AtA; R = AtB;
  for (i = 0; i < n; i++) {
    int piv = i;
    double pv, t;
    for (l = i + 1; l < n; l++) if (fabs(M[l * n + i]) > fabs(M[piv * n + i])) piv = l;
    if (fabs(M[piv * n + i]) < 1e-300) {
      free(AtA); free(AtB);
      rt_error("DOMAIN ERROR: domino: singular matrix");
    }
    if (piv != i) {
      for (j = 0; j < n; j++) { t = M[i * n + j]; M[i * n + j] = M[piv * n + j]; M[piv * n + j] = t; }
      for (j = 0; j < k; j++) { t = R[i * k + j]; R[i * k + j] = R[piv * k + j]; R[piv * k + j] = t; }
    }
    pv = M[i * n + i];
    for (l = 0; l < n; l++) {
      double f;
      if (l == i) continue;
      f = M[l * n + i] / pv;
      if (f == 0.0) continue;
      for (j = 0; j < n; j++) M[l * n + j] -= f * M[i * n + j];
      for (j = 0; j < k; j++) R[l * k + j] -= f * R[i * k + j];
    }
  }
  if (bl && vec) { rank = 1; sh[0] = n; }
  else { rank = 2; sh[0] = n; sh[1] = k; }
  r = a_new(T_NUM, rank, sh);
  for (i = 0; i < n; i++)
    for (j = 0; j < k; j++) r->num[i * k + j] = R[i * k + j] / M[i * n + i];
  free(AtA); free(AtB);
  return r;
}

static Arr *partition_p(Arr *a, Arr *x)
{
  int i, start = -1, groups = 0, g = 0, prev = 0;
  Arr *r;
  need_num(a, P_PARTITION);
  if (a->rank > 1 || x->rank > 1) rt_error("RANK ERROR: partition handles vectors");
  if (a->n != (x->rank == 0 ? 1 : x->n)) rt_error("LENGTH ERROR: partition");
  for (i = 0; i < a->n; i++) {
    int v = as_int(a->num[i], P_PARTITION);
    if (v < 0) rt_error("DOMAIN ERROR: partition needs non-negative numbers");
    if (v > 0 && v > prev) groups++;
    prev = v;
  }
  r = a_vec(T_NEST, groups);
  prev = 0;
  for (i = 0; i <= a->n; i++) {
    int v = (i < a->n) ? (int)a->num[i] : 0;
    int boundary = (i == a->n) || v == 0 || v > prev;
    if (boundary && start >= 0) {
      Arr *seg = a_vec(x->type, i - start);
      int k;
      for (k = start; k < i; k++) a_copyel(seg, k - start, x, k);
      a_put(r, g++, seg);
      start = -1;
    }
    if (i < a->n && v > 0 && start < 0) start = i;
    prev = v;
  }
  return r;
}

/* ---------- nested-array primitives (APL2) ---------- */
static Arr *enclose_p(Arr *x)
{
  Arr *r;
  if (a_simple_scalar(x)) return a_ref(x);
  r = a_new(T_NEST, 0, NULL);
  a_put(r, 0, a_ref(x));
  return r;
}

static Arr *proto_scalar(const Arr *x)
{
  if (x->type == T_CHR) return a_char(' ');
  if (x->type == T_NEST && x->n > 0 && x->sub[0] &&
      x->sub[0]->type == T_CHR && x->sub[0]->rank == 0) return a_char(' ');
  return a_scalar(0.0);
}

static Arr *first_p(Arr *x)
{
  if (x->n == 0) return proto_scalar(x);
  return a_item(x, 0);
}

static Arr *disclose_p(Arr *x)
{
  int i, k, R = 0, outer, inner = 1, rank;
  int ms[MAXRANK], sh[MAXRANK] = {0}, idx[MAXRANK];
  Arr *r;
  if (x->type != T_NEST) return a_ref(x);
  for (i = 0; i < x->n; i++)
    if (x->sub[i]->rank > R) R = x->sub[i]->rank;
  if (x->rank + R > MAXRANK) rt_error("RANK ERROR");
  for (k = 0; k < R; k++) ms[k] = 1;
  for (i = 0; i < x->n; i++) {
    Arr *it = x->sub[i];
    int off = R - it->rank;
    for (k = 0; k < it->rank; k++)
      if (it->shape[k] > ms[off + k]) ms[off + k] = it->shape[k];
  }
  rank = x->rank + R;
  for (k = 0; k < x->rank; k++) sh[k] = x->shape[k];
  for (k = 0; k < R; k++) { sh[x->rank + k] = ms[k]; inner *= ms[k]; }
  outer = x->n;
  r = a_new(T_NEST, rank, sh);
  for (i = 0; i < outer; i++) {
    Arr *it = x->sub[i];
    int off = R - it->rank, j;
    for (k = 0; k < R; k++) idx[k] = 0;
    for (j = 0; j < inner; j++) {
      int ok = 1, lin = 0, st = 1;
      for (k = R - 1; k >= 0; k--) {
        int ik = (k >= off) ? idx[k] : 0;
        if (k >= off) {
          if (idx[k] >= it->shape[k - off]) ok = 0;
          lin += ik * st;
          st *= it->shape[k - off];
        } else if (idx[k] != 0) ok = 0;
      }
      if (ok) a_put(r, i * inner + j, a_item(it, lin));
      else a_put(r, i * inner + j, proto_scalar(it));
      for (k = R - 1; k >= 0; k--) {
        if (++idx[k] < ms[k]) break;
        idx[k] = 0;
      }
    }
  }
  return r;
}

static Arr *pick_p(Arr *left, Arr *x)
{
  Arr *cur = a_ref(x);
  int i;
  for (i = 0; i < left->n; i++) {
    Arr *ix = a_item(left, i);
    int lin = 0, st = 1, k, nxt;
    Arr *item;
    need_num(ix, P_PICK);
    if (ix->rank > 1 || (cur->rank == 0 ? 0 : ix->n != cur->rank) ||
        (cur->rank == 0 && ix->n != 0 && ix->n != 1)) {
      a_rel(ix); a_rel(cur);
      rt_error("pick: index length must equal rank");
    }
    if (cur->rank == 0) {
      lin = 0;
      if (ix->n == 1 && as_int(ix->num[0], P_PICK) - Q_IO != 0) {
        a_rel(ix); a_rel(cur);
        rt_error("index: subscript out of range");
      }
    } else {
      for (k = cur->rank - 1; k >= 0; k--) {
        int s = as_int(ix->num[k], P_PICK) - Q_IO;
        if (s < 0 || s >= cur->shape[k]) {
          a_rel(ix); a_rel(cur);
          rt_error("index: subscript out of range");
        }
        lin += s * st;
        st *= cur->shape[k];
      }
    }
    nxt = lin;
    item = a_item(cur, nxt);
    a_rel(ix); a_rel(cur);
    cur = item;
  }
  return cur;
}

static Arr *match_p(Arr *x, Arr *y)
{
  return a_scalar(a_match(x, y) ? 1.0 : 0.0);
}
static Arr *depth_p(Arr *x) { return a_scalar((double)a_depth(x)); }
static Arr *tally_p(Arr *x)
{
  return a_scalar(x->rank == 0 ? 1.0 : (double)x->shape[0]);
}

static Arr *member_p(Arr *x, Arr *y)
{
  Arr *r = a_new(T_NUM, x->rank, x->shape);
  int i, j;
  for (i = 0; i < x->n; i++) {
    Arr *e = a_item(x, i);
    int found = 0;
    for (j = 0; j < y->n && !found; j++) {
      Arr *f = a_item(y, j);
      found = a_match(e, f);
      a_rel(f);
    }
    a_rel(e);
    r->num[i] = found ? 1.0 : 0.0;
  }
  return r;
}

static int enlist_count(const Arr *a)
{
  int i, c = 0;
  if (a->type != T_NEST) return a->n;
  for (i = 0; i < a->n; i++) c += enlist_count(a->sub[i]);
  return c;
}
static void enlist_fill(const Arr *a, Arr *out, int *k)
{
  int i;
  if (a->type != T_NEST) {
    for (i = 0; i < a->n; i++) a_put(out, (*k)++, a_item(a, i));
    return;
  }
  for (i = 0; i < a->n; i++) enlist_fill(a->sub[i], out, k);
}
static Arr *enlist_p(Arr *x)
{
  int n = enlist_count(x), k = 0;
  Arr *r = a_vec(T_NEST, n);
  enlist_fill(x, r, &k);
  return r;
}

static Arr *deal_p(Arr *x, Arr *y)
{
  int m, n, i, j;
  Arr *pool, *r;
  need_num(x, P_DEAL); need_num(y, P_DEAL);
  if (x->n != 1 || y->n != 1) rt_error("DOMAIN ERROR: deal needs scalars");
  m = as_int(x->num[0], P_DEAL); n = as_int(y->num[0], P_DEAL);
  if (m < 0 || m > n) rt_error("DOMAIN ERROR: deal: count exceeds range");
  pool = a_vec(T_NUM, n);
  for (i = 0; i < n; i++) pool->num[i] = (double)(Q_IO + i);
  r = a_vec(T_NUM, m);
  for (i = 0; i < m; i++) {
    j = i + (int)(rnd_next() * (double)(n - i));
    r->num[i] = pool->num[j];
    pool->num[j] = pool->num[i];
  }
  a_rel(pool);
  return r;
}

static Arr *notmatch_p(Arr *x, Arr *y)
{
  Arr *m = match_p(x, y), *r;
  r = a_scalar(m->num[0] != 0.0 ? 0.0 : 1.0);
  a_rel(m);
  return r;
}

/* characters <-> code points */
static Arr *ucs_p(Arr *x)
{
  Arr *r;
  int i;
  if (x->type == T_CHR) {
    r = a_new(T_NUM, x->rank, x->shape);
    for (i = 0; i < x->n; i++) r->num[i] = (double)(unsigned char)x->chr[i];
    return r;
  }
  need_num(x, P_UCS);
  r = a_new(T_CHR, x->rank, x->shape);
  for (i = 0; i < x->n; i++) {
    int c = as_int(x->num[i], P_UCS);
    if (c < 0 || c > 255) { a_rel(r); rt_error("DOMAIN ERROR: ucs code out of range 0..255"); }
    r->chr[i] = (char)c;
  }
  return r;
}

/* nest: enclose a simple array, leave scalars and nested arrays alone */
static Arr *nest_p(Arr *x)
{
  if (x->rank == 0 || x->type == T_NEST) return a_ref(x);
  return enclose_p(x);
}

static Arr *prim_mon0(int id, Arr *x)
{
  int ax = cur_axis;
  cur_axis = -1;
  switch (id) {
  case P_RHO: return shape_of(x);
  case P_IOTA: return iota_p(x);
  case P_RAVEL: return ravel_p(x);
  case P_REVERSE: return reverse_p(x, ax);
  case P_GRADEUP: return grade_p(NULL, x, 0);
  case P_GRADEDN: return grade_p(NULL, x, 1);
  case P_UNIQUE: return unique_p(x);
  case P_NUBSIEVE: return nubsieve_p(x);
  case P_WHERE: return where_p(x);
  case P_SPLIT: return split_p(x);
  case P_RIGHT: case P_LEFT: return a_ref(x);
  case P_DOMINO: return domino_p(NULL, x);
  case P_FORMAT: return format_mon(x);
  case P_EXECUTE: return execute_p(x);
  case P_TRANSPOSE: return transpose_p(x);
  case P_ENCLOSE: return enclose_p(x);
  case P_DISCLOSE: return disclose_p(x);
  case P_FIRST: return first_p(x);
  case P_DEPTH: return depth_p(x);
  case P_TALLY: return tally_p(x);
  case P_ENLIST: return enlist_p(x);
  case P_UCS: return ucs_p(x);
  case P_NEST: return nest_p(x);
  default:
    if (is_scalar_mon(id)) return scalar_mon(id, x);
  }
  rt_error_s("", prim_name(id), " is not a monadic function");
  return NULL;
}

static Arr *prim_dya0(int id, Arr *x, Arr *y)
{
  int ax = cur_axis;
  cur_axis = -1;
  switch (id) {
  case P_RHO: return reshape(x, y);
  case P_ROTATE: return rotate_p(x, y, ax);
  case P_TRANSPOSE: return transpose_g(x, y);
  case P_EXPAND: return expand_p(x, y, ax);
  case P_GRADEUP: return grade_p(x, y, 0);
  case P_GRADEDN: return grade_p(x, y, 1);
  case P_INDEXOF: return indexof_p(x, y);
  case P_UNION: return union_p(x, y);
  case P_INTERSECT: return intersect_p(x, y);
  case P_WITHOUT: return without_p(x, y);
  case P_FIND: return find_p(x, y);
  case P_FORMAT: return format_dya(x, y);
  case P_ENCODE: return encode_p(x, y);
  case P_DECODE: return decode_p(x, y);
  case P_DOMINO: return domino_p(x, y);
  case P_PARTITION: return partition_p(x, y);
  case P_RIGHT: return a_ref(y);
  case P_LEFT: return a_ref(x);
  case P_TAKE: return take_drop(x, y, 1);
  case P_DROP: return take_drop(x, y, 0);
  case P_CAT: return cat_p(x, y, ax);
  case P_COMPRESS: return compress_p(x, y, ax);
  case P_INDEX: return index_p(x, y);
  case P_MATMUL: return matmul_p(x, y);
  case P_PICK: return pick_p(x, y);
  case P_MATCH: return match_p(x, y);
  case P_NOTMATCH: return notmatch_p(x, y);
  case P_MEMBER: return member_p(x, y);
  case P_DEAL: return deal_p(x, y);
  default:
    if (is_scalar_dya(id)) return scalar_dya(id, x, y);
  }
  rt_error_s("", prim_name(id), " is not a dyadic function");
  return NULL;
}

/* every result is normalised: an all-simple nested array is stored flat */
static Arr *prim_mon(int id, Arr *x) { return a_simplify(prim_mon0(id, x)); }
static Arr *prim_dya(int id, Arr *x, Arr *y)
{
  return a_simplify(prim_dya0(id, x, y));
}
/* ===== p3b_ap.c ===== */
/* p3b_ap.c - auxiliary processors and shared variables (the hooks)
 *
 * A processor is a numbered C module that owns some variables.  The user
 * couples a variable to a processor with   n []SVO 'NAME'   Afterwards
 *   - every reference to NAME calls the processor's get hook,
 *   - every assignment to NAME calls its set hook,
 *   - []SVR 'NAME' retracts the offer.
 * To add a processor, write the hooks below and call ap_register() from
 * ap_init().  A NULL hook means "do nothing", so a variable simply holds its
 * value.  Hooks signal errors with rt_error() like any primitive.
 *
 *   offer(name)         called when the offer is made; may reject it
 *   get(name)           returns a NEW value to deliver, or NULL to deliver
 *                       the value last stored
 *   set(name, value)    called after assignment; returns a NEW value to store
 *                       in place of `value`, or NULL to keep `value`
 *   retract(name)       called when the offer ends
 */
#define AP_MAXPROC 32

typedef struct ApProc {
  int id;
  const char *name;
  int installed;                       /* 0: reserved number, no code yet */
  void (*offer)(const char *var);
  Arr *(*get)(const char *var);
  Arr *(*set)(const char *var, Arr *val);
  void (*retract)(const char *var);
} ApProc;

static ApProc ap_tab[AP_MAXPROC];
static int ap_n = 0;

static void ap_register(int id, const char *name, int installed,
                        void (*offer)(const char *),
                        Arr *(*get)(const char *),
                        Arr *(*set)(const char *, Arr *),
                        void (*retract)(const char *))
{
  ApProc *p;
  if (ap_n >= AP_MAXPROC) return;
  p = &ap_tab[ap_n++];
  p->id = id; p->name = name; p->installed = installed;
  p->offer = offer; p->get = get; p->set = set; p->retract = retract;
}

static ApProc *ap_find(int id)
{
  int i;
  for (i = 0; i < ap_n; i++) if (ap_tab[i].id == id) return &ap_tab[i];
  return NULL;
}

/* ---- the call-outs used by the variable layer ---- */
static void ap_do_offer(int id, const char *var)
{
  ApProc *p = ap_find(id);
  char buf[96];
  if (!p) {
    sprintf(buf, "no such auxiliary processor: %d", id);
    rt_error(buf);
  }
  if (!p->installed) {
    sprintf(buf, "auxiliary processor %d (%s) is reserved but not installed",
            id, p->name);
    rt_error(buf);
  }
  if (p->offer) p->offer(var);
}
static Arr *ap_do_get(int id, const char *var)
{
  ApProc *p = ap_find(id);
  return (p && p->get) ? p->get(var) : NULL;
}
static Arr *ap_do_set(int id, const char *var, Arr *val)
{
  ApProc *p = ap_find(id);
  return (p && p->set) ? p->set(var, val) : NULL;
}
static void ap_do_retract(int id, const char *var)
{
  ApProc *p = ap_find(id);
  if (p && p->retract) p->retract(var);
}

/* ---- built-in processors ---- */

/* AP 2: UPPER - whatever text is assigned comes back in capitals */
static Arr *up_set(const char *var, Arr *val)
{
  Arr *r;
  int i;
  (void)var;
  if (val->type != T_CHR) rt_error("DOMAIN ERROR: AP 2 (UPPER) accepts text only");
  r = a_new(T_CHR, val->rank, val->shape);
  for (i = 0; i < val->n; i++) r->chr[i] = (char)to_upper((unsigned char)val->chr[i]);
  return r;
}

/* AP 3: COUNTER - each reference yields the next integer; assigning resets */
static double ap_counter = 0.0;
static Arr *ctr_get(const char *var)
{
  (void)var;
  ap_counter += 1.0;
  return a_scalar(ap_counter);
}
static Arr *ctr_set(const char *var, Arr *val)
{
  (void)var;
  if (val->type != T_NUM || val->n != 1)
    rt_error("DOMAIN ERROR: AP 3 (COUNTER) needs a numeric scalar");
  ap_counter = val->num[0];
  return NULL;
}

static void ap_init(void)
{
  ap_n = 0;
  ap_register(1, "LOOPBACK", 1, NULL, NULL, NULL, NULL);
  ap_register(2, "UPPER", 1, NULL, NULL, up_set, NULL);
  ap_register(3, "COUNTER", 1, NULL, ctr_get, ctr_set, NULL);
  /* numbers reserved for processors that are not written yet */
  ap_register(124, "AP124", 0, NULL, NULL, NULL, NULL);
  ap_register(126, "AP126", 0, NULL, NULL, NULL, NULL);
  ap_register(210, "AP210", 0, NULL, NULL, NULL, NULL);
}
/* ===== p4_eval.c ===== */
/* p4_eval.c - variables, user functions, parser/evaluator, operators */

/* ---------- variables ---------- */
typedef struct Var {
  char *name;
  Arr *val;
  int ap;                  /* processor this variable is shared with, or 0 */
  struct Var *next;
} Var;
#define HSIZE 211
static Var *vtab[HSIZE];

static unsigned hashname(const char *s)
{
  unsigned h = 5381u;
  while (*s) h = h * 33u + (unsigned char)*s++;
  return h % HSIZE;
}
static Var *var_find(const char *name, int create)
{
  unsigned h = hashname(name);
  Var *v;
  for (v = vtab[h]; v; v = v->next)
    if (strcmp(v->name, name) == 0) return v;
  if (!create) return NULL;
  v = (Var *)xmalloc(sizeof(Var));
  v->name = xstrdup(name);
  v->val = NULL;
  v->ap = 0;
  v->next = vtab[h];
  vtab[h] = v;
  return v;
}
static Arr *var_get(const char *name)
{
  Var *v = var_find(name, 0);
  if (!v) return NULL;
  if (v->ap) {
    Arr *g = ap_do_get(v->ap, name);
    if (g) { a_rel(v->val); v->val = g; }
  }
  return v->val;
}
/* takes ownership of one reference to a (may be NULL: unbind) */
static void var_set(const char *name, Arr *a)
{
  Var *v = var_find(name, a != NULL);
  if (!v) return;
  if (v->ap) {
    if (a) {
      Arr *rep = ap_do_set(v->ap, name, a);
      if (rep) { a_rel(a); a = rep; }
    } else {
      int id = v->ap;
      v->ap = 0;
      ap_do_retract(id, name);
    }
  }
  a_rel(v->val);
  v->val = a;
}
/* detach the current value (caller owns it) */
static Arr *var_take(const char *name)
{
  Var *v = var_find(name, 0);
  Arr *a;
  if (!v) return NULL;
  a = v->val;
  v->val = NULL;
  return a;
}

/* ---------- user functions ---------- */
typedef struct Fn {
  char *name;
  int arity;                 /* 0, 1, 2 */
  char *res, *left, *right;  /* NULL when absent */
  char *hdr;                 /* header text as shown */
  int nloc;
  char **loc;
  int nlines;                /* lines are 1..nlines */
  char **lines;              /* [0] unused */
  Tok **tk;                  /* lazily lexed */
  char **label;              /* label name per line or NULL */
  int lam;                   /* 1: a { } lambda (statements in stk) */
  int ambi;                  /* callable monadically or dyadically */
  int nstmt;
  Tok **stk;                 /* lambda statements, each END-terminated */
  int opn;                   /* user-defined operator: number of operands */
  Tok *optok;                /* its body tokens (END-terminated) */
  int cbuilt, hasctl;        /* structured control flow tables */
  int *ck, *cj;              /* kind per line; jump targets (3 per line) */
} Fn;

/* ---------- localization frames ---------- */
typedef struct Saved { const char *name; Arr *val; } Saved;
typedef struct Frame { Fn *fn; int nsaved; Saved *sv; } Frame;
static Frame frames[MAXDEPTH + 2];
static int fdepth = 0;

static void unwind_frames(int depth)
{
  while (fdepth > depth) {
    Frame *fr = &frames[--fdepth];
    int i;
    for (i = fr->nsaved - 1; i >= 0; i--)
      var_set(fr->sv[i].name, fr->sv[i].val);
    free(fr->sv);
  }
}

static Fn **fns = NULL;
static int nfns = 0;

static Fn *fn_find(const char *name)
{
  int i;
  if (name[0] == 's' && strcmp(name, "self") == 0) {
    if (fdepth > 0 && frames[fdepth - 1].fn->lam) return frames[fdepth - 1].fn;
    return NULL;
  }
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->name, name) == 0) return fns[i];
  return NULL;
}
static void fn_free(Fn *f)
{
  int i;
  free(f->name); free(f->res); free(f->left); free(f->right); free(f->hdr);
  for (i = 0; i < f->nloc; i++) free(f->loc[i]);
  free(f->loc);
  for (i = 1; i <= f->nlines; i++) {
    free(f->lines[i]);
    lex_free(f->tk[i]);
    free(f->label[i]);
  }
  free(f->lines); free(f->tk); free(f->label);
  for (i = 0; i < f->nstmt; i++) lex_free(f->stk[i]);
  free(f->stk);
  free(f->ck); free(f->cj);
  if (f->optok) lex_free(f->optok);
  free(f);
}
static void fn_install(Fn *f)
{
  int i;
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->name, f->name) == 0) {
      fn_free(fns[i]);
      fns[i] = f;
      return;
    }
  fns = (Fn **)xrealloc(fns, sizeof(Fn *) * (size_t)(nfns + 1));
  fns[nfns++] = f;
}
static int fn_remove(const char *name)
{
  int i;
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->name, name) == 0) {
      fn_free(fns[i]);
      for (; i + 1 < nfns; i++) fns[i] = fns[i + 1];
      nfns--;
      return 1;
    }
  return 0;
}

/* ---------- branch state ---------- */
static int br_set = 0;
static int br_target = 0;

/* ---------- parser state ---------- */
typedef struct Ps { Tok *t; int i; } Ps;

typedef struct FnRef {
  int prim;        /* primitive id, or -1 */
  Fn *user;
  int axis;        /* [k] after the function, or -1 */
  int ntok;        /* tokens the function occupies (lambdas span several) */
} FnRef;

static Tok *tok_slice(const Tok *t, int a, int b);
static char *tok_src(const Tok *t, int a, int b);
static Arr *parse_expr(Ps *p);
static Arr *apply_userop(Ps *p, Fn *u, Arr *left);
static int quad_is_fn(const char *name);
static Arr *quad_fn(const char *name, Arr *x);
static Arr *quad_svo(Arr *proc, Arr *names);
static Fn *lambda_at(Ps *p, int *ntok);
static int brace_end(const Tok *t, int i);
static Fn *lambda_make(const Tok *t, int a, int b, const char *name);
static Arr *call_fn(Fn *f, Arr *left, Arr *right);
static Arr *eval_text(const char *s);

static Arr *need_val(Arr *a)
{
  if (!a) syn_error("function returned no value");
  return a;
}

static Arr *fn_mon(const FnRef *f, Arr *x)
{
  if (f->user) {
    if (f->user->arity != 1 && !f->user->ambi) syn_error("function is not monadic");
    return need_val(call_fn(f->user, NULL, x));
  }
  return prim_mon(f->prim, x);
}
static Arr *fn_dya(const FnRef *f, Arr *x, Arr *y)
{
  if (f->user) {
    if (f->user->arity != 2) syn_error("function is not dyadic");
    return need_val(call_fn(f->user, x, y));
  }
  return prim_dya(f->prim, x, y);
}

static double fold_pair(const FnRef *f, double a, double b)
{
  Arr *A, *B, *R;
  double v;
  if (!f->user && is_scalar_dya(f->prim)) return sdya(f->prim, a, b);
  A = a_scalar(a); B = a_scalar(b);
  R = fn_dya(f, A, B);
  if (R->type != T_NUM || R->n != 1) {
    a_rel(A); a_rel(B); a_rel(R);
    rt_error("operator: function must return a scalar");
  }
  v = R->num[0];
  a_rel(A); a_rel(B); a_rel(R);
  return v;
}

static void need_scalar_fn(const FnRef *f, const char *op)
{
  if (f->user) {
    if (f->user->arity == 2) return;
  } else if (is_scalar_dya(f->prim)) return;
  rt_error_s(op, ": needs a scalar dyadic function", "");
}

static double prim_identity(const FnRef *f)
{
  if (!f->user)
    switch (f->prim) {
    case P_ADD: case P_SUB: case P_OR: case P_NE: case P_LT: case P_GT:
      return 0.0;
    case P_MUL: case P_DIV: case P_AND: case P_EQ: case P_LE: case P_GE:
    case P_POW:
      return 1.0;
    case P_MAX: return -APL_NUM_MAX;
    case P_MIN: return APL_NUM_MAX;
    default: break;
    }
  rt_error("DOMAIN ERROR: reduce of an empty vector has no identity element");
  return 0.0;
}

static Arr *op_reduce(const FnRef *f, Arr *x, int ax)
{
  int k, outer, len, inner, o, j, i, sh[MAXRANK], rank, m;
  Arr *r;
  if (x->rank == 0) return a_ref(x);
  k = axis_pick(x, ax, P_REDUCE);
  axis_dims(x, k, &outer, &len, &inner);
  rank = x->rank - 1;
  for (i = 0, m = 0; i < x->rank; i++) if (i != k) sh[m++] = x->shape[i];
  if (len == 0) {
    double idv = prim_identity(f);
    r = a_new(T_NUM, rank, sh);
    for (i = 0; i < r->n; i++) r->num[i] = idv;
    return r;
  }
  if (x->type == T_NEST) {
    r = a_new(T_NEST, rank, sh);
    for (o = 0; o < outer; o++)
      for (i = 0; i < inner; i++) {
        Arr *acc = a_item(x, (o * len + len - 1) * inner + i);
        for (j = len - 2; j >= 0; j--) {
          Arr *e = a_item(x, (o * len + j) * inner + i);
          Arr *v = fn_dya(f, e, acc);
          a_rel(e); a_rel(acc);
          acc = v;
        }
        a_put(r, o * inner + i, acc);
      }
    return a_simplify(r);
  }
  need_scalar_fn(f, "reduce");
  need_num(x, P_REDUCE);
  r = a_new(T_NUM, rank, sh);
  for (o = 0; o < outer; o++)
    for (i = 0; i < inner; i++) {
      double acc = x->num[(o * len + len - 1) * inner + i];
      for (j = len - 2; j >= 0; j--)
        acc = fold_pair(f, x->num[(o * len + j) * inner + i], acc);
      r->num[o * inner + i] = acc;
    }
  return r;
}

static Arr *op_scan(const FnRef *f, Arr *x, int ax)
{
  int k, outer, len, inner, o, j, i, q;
  Arr *r;
  if (x->rank == 0) return a_ref(x);
  k = axis_pick(x, ax, P_SCAN);
  axis_dims(x, k, &outer, &len, &inner);
  if (x->type == T_NEST) {
    r = a_new(T_NEST, x->rank, x->shape);
    for (o = 0; o < outer; o++)
      for (i = 0; i < inner; i++)
        for (j = 0; j < len; j++) {
          Arr *acc = a_item(x, (o * len + j) * inner + i);
          for (q = j - 1; q >= 0; q--) {
            Arr *e = a_item(x, (o * len + q) * inner + i);
            Arr *v = fn_dya(f, e, acc);
            a_rel(e); a_rel(acc);
            acc = v;
          }
          a_put(r, (o * len + j) * inner + i, acc);
        }
    return a_simplify(r);
  }
  need_scalar_fn(f, "scan");
  need_num(x, P_SCAN);
  r = a_new(T_NUM, x->rank, x->shape);
  for (o = 0; o < outer; o++)
    for (i = 0; i < inner; i++)
      for (j = 0; j < len; j++) {
        /* element j is the reduction of the prefix 0..j, right to left */
        double acc = x->num[(o * len + j) * inner + i];
        for (q = j - 1; q >= 0; q--)
          acc = fold_pair(f, x->num[(o * len + q) * inner + i], acc);
        r->num[(o * len + j) * inner + i] = acc;
      }
  return r;
}

static Arr *op_each(const FnRef *f, Arr *x)
{
  Arr *r;
  int i;
  if (!f->user && is_scalar_mon(f->prim)) return scalar_mon(f->prim, x);
  r = a_new(T_NEST, x->rank, x->shape);
  for (i = 0; i < x->n; i++) {
    Arr *e = a_item(x, i);
    Arr *v = fn_mon(f, e);
    a_rel(e);
    a_put(r, i, v);
  }
  return a_simplify(r);
}

static Arr *op_outer(const FnRef *f, Arr *x, Arr *y)
{
  int sh[MAXRANK], i, j, k, rank;
  Arr *r;
  rank = x->rank + y->rank;
  if (rank > MAXRANK) rt_error("RANK ERROR");
  for (i = 0; i < x->rank; i++) sh[i] = x->shape[i];
  for (j = 0; j < y->rank; j++) sh[x->rank + j] = y->shape[j];
  if (x->type == T_NEST || y->type == T_NEST || x->type == T_CHR ||
      y->type == T_CHR) {
    r = a_new(T_NEST, rank, sh);
    k = 0;
    for (i = 0; i < x->n; i++) {
      Arr *e = a_item(x, i);
      for (j = 0; j < y->n; j++) {
        Arr *g = a_item(y, j);
        a_put(r, k++, fn_dya(f, e, g));
        a_rel(g);
      }
      a_rel(e);
    }
    return a_simplify(r);
  }
  need_scalar_fn(f, "outer");
  r = a_new(T_NUM, rank, sh);
  k = 0;
  for (i = 0; i < x->n; i++)
    for (j = 0; j < y->n; j++)
      r->num[k++] = fold_pair(f, x->num[i], y->num[j]);
  return r;
}

/* dyadic each: pairwise application, scalar/one-item extension */
static Arr *op_each2(const FnRef *f, Arr *x, Arr *y)
{
  Arr *r;
  int i, xs = (x->n == 1 && x->rank == 0) || (x->n == 1 && y->n != 1);
  int ys = (y->n == 1 && y->rank == 0) || (y->n == 1 && x->n != 1);
  const Arr *sh = x;
  if (xs && !ys) sh = y;
  else if (!xs && !ys && !a_same_shape(x, y)) {
    char buf[96];
    sprintf(buf, "LENGTH ERROR: each shape mismatch: %d / %d", x->n, y->n);
    rt_error(buf);
  }
  if (xs && ys) sh = (x->rank >= y->rank) ? x : y;
  r = a_new(T_NEST, sh->rank, sh->shape);
  for (i = 0; i < r->n; i++) {
    Arr *e = a_item(x, xs ? 0 : i);
    Arr *g = a_item(y, ys ? 0 : i);
    Arr *v = fn_dya(f, e, g);
    a_rel(e); a_rel(g);
    a_put(r, i, v);
  }
  return a_simplify(r);
}

/* x f.g y : generalised inner product */
static Arr *op_inner(const FnRef *f, const FnRef *g, Arr *x, Arr *y)
{
  int k, m, n, rank, sh[MAXRANK], i, j, l, a;
  Arr *r;
  if (x->rank == 0 || y->rank == 0) {
    Arr *xs = (x->rank == 0) ? x : NULL;
    (void)xs;
    rt_error("RANK ERROR: inner product needs arrays, not scalars");
  }
  k = x->shape[x->rank - 1];
  if (k != y->shape[0]) {
    char buf[96];
    sprintf(buf, "LENGTH ERROR: inner: %d / %d", k, y->shape[0]);
    rt_error(buf);
  }
  if (k == 0) rt_error("inner: empty axis");
  rank = x->rank - 1 + y->rank - 1;
  if (rank > MAXRANK) rt_error("RANK ERROR");
  a = 0;
  for (i = 0; i < x->rank - 1; i++) sh[a++] = x->shape[i];
  for (j = 1; j < y->rank; j++) sh[a++] = y->shape[j];
  m = x->n / k;
  n = y->n / k;
  if (x->type == T_NUM && y->type == T_NUM && !g->user && !f->user &&
      is_scalar_dya(g->prim) && is_scalar_dya(f->prim)) {
    r = a_new(T_NUM, rank, sh);
    for (i = 0; i < m; i++)
      for (j = 0; j < n; j++) {
        double acc = sdya(g->prim, x->num[i * k + k - 1], y->num[(k - 1) * n + j]);
        for (l = k - 2; l >= 0; l--)
          acc = sdya(f->prim, sdya(g->prim, x->num[i * k + l], y->num[l * n + j]), acc);
        r->num[i * n + j] = acc;
      }
    return r;
  }
  r = a_new(T_NEST, rank, sh);
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++) {
      Arr *acc, *e1, *e2;
      e1 = a_item(x, i * k + k - 1); e2 = a_item(y, (k - 1) * n + j);
      acc = fn_dya(g, e1, e2);
      a_rel(e1); a_rel(e2);
      for (l = k - 2; l >= 0; l--) {
        Arr *t, *v;
        e1 = a_item(x, i * k + l); e2 = a_item(y, l * n + j);
        t = fn_dya(g, e1, e2);
        a_rel(e1); a_rel(e2);
        v = fn_dya(f, t, acc);
        a_rel(t); a_rel(acc);
        acc = v;
      }
      a_put(r, i * n + j, acc);
    }
  return a_simplify(r);
}

/* n f/ x : reduce over sliding windows of length n along an axis */
static Arr *op_nreduce(const FnRef *f, Arr *nn, Arr *x, int ax)
{
  int n, k, outer, len, inner, o, j, i, q, w, sh[MAXRANK], m;
  Arr *r;
  need_num(nn, P_REDUCE);
  if (nn->n != 1) rt_error("DOMAIN ERROR: window size must be a scalar");
  n = as_int(nn->num[0], P_REDUCE);
  if (n < 0) rt_error("DOMAIN ERROR: negative window size");
  if (x->rank == 0) rt_error("RANK ERROR: n-wise reduce needs an array");
  k = axis_pick(x, ax, P_REDUCE);
  axis_dims(x, k, &outer, &len, &inner);
  if (n > len + 1) rt_error("LENGTH ERROR: window larger than axis");
  if (n == 0) rt_error("DOMAIN ERROR: zero window");
  w = len - n + 1;
  for (i = 0, m = 0; i < x->rank; i++) sh[m++] = (i == k) ? w : x->shape[i];
  r = a_new(T_NEST, x->rank, sh);
  for (o = 0; o < outer; o++)
    for (q = 0; q < w; q++)
      for (i = 0; i < inner; i++) {
        Arr *acc = a_item(x, (o * len + q + n - 1) * inner + i);
        for (j = n - 2; j >= 0; j--) {
          Arr *e = a_item(x, (o * len + q + j) * inner + i);
          Arr *v = fn_dya(f, e, acc);
          a_rel(e); a_rel(acc);
          acc = v;
        }
        a_put(r, (o * w + q) * inner + i, acc);
      }
  return a_simplify(r);
}

/* split z into cells of rank c; frame shape goes to fsh */
static int rank_cells(Arr *z, int c, int *fsh, int *fr, int *csz, int *cshp)
{
  int i, nc = 1, cs = 1;
  if (c < 0) c = (z->rank + c > 0) ? z->rank + c : 0;
  if (c > z->rank) c = z->rank;
  *fr = z->rank - c;
  for (i = 0; i < *fr; i++) { fsh[i] = z->shape[i]; nc *= fsh[i]; }
  for (i = 0; i < c; i++) { cshp[i] = z->shape[*fr + i]; cs *= cshp[i]; }
  *csz = cs;
  return nc;
}

static Arr *take_cell(Arr *z, int i, int c, int fr, const int *cshp, int csz)
{
  Arr *cell = a_like(z, z->rank - fr, cshp);
  int k;
  (void)c;
  for (k = 0; k < csz; k++) a_copyel(cell, k, z, i * csz + k);
  return cell;
}

static Arr *op_rank(const FnRef *f, Arr *kv, Arr *x, Arr *y)
{
  int km, kl, kr, i, cx = 0, cy = 0;
  int fsx[MAXRANK] = {0}, fsy[MAXRANK] = {0}, csx[MAXRANK] = {0}, csy[MAXRANK] = {0};
  int frx, fry, szx = 1, szy = 1, ncx, ncy, nc = 0;
  Arr *r, *res;
  need_num(kv, P_RANK);
  if (kv->n < 1 || kv->n > 3) rt_error("DOMAIN ERROR: rank needs 1 to 3 numbers");
  if (kv->n == 1) { km = kl = kr = as_int(kv->num[0], P_RANK); }
  else if (kv->n == 2) { kl = as_int(kv->num[0], P_RANK); km = kr = as_int(kv->num[1], P_RANK); }
  else { km = as_int(kv->num[0], P_RANK); kl = as_int(kv->num[1], P_RANK); kr = as_int(kv->num[2], P_RANK); }
  if (!y) {
    ncx = rank_cells(x, km, fsx, &frx, &szx, csx);
    r = a_new(T_NEST, frx, fsx);
    for (i = 0; i < ncx; i++) {
      Arr *cell = take_cell(x, i, 0, frx, csx, szx);
      Arr *v = fn_mon(f, cell);
      a_rel(cell);
      a_put(r, i, v);
    }
    r = a_simplify(r);
    res = disclose_p(r);
    a_rel(r);
    return a_simplify(res);
  }
  ncx = rank_cells(x, kl, fsx, &frx, &szx, csx);
  ncy = rank_cells(y, kr, fsy, &fry, &szy, csy);
  {
    int same = (frx == fry);
    for (i = 0; same && i < frx; i++) if (fsx[i] != fsy[i]) same = 0;
    if (same) nc = ncx;
    else if (frx == 0) nc = ncy;
    else if (fry == 0) nc = ncx;
    else rt_error("LENGTH ERROR: rank frames do not match");
    r = a_new(T_NEST, same ? frx : (frx == 0 ? fry : frx), same ? fsx : (frx == 0 ? fsy : fsx));
    (void)cx; (void)cy;
    for (i = 0; i < nc; i++) {
      Arr *a = take_cell(x, frx == 0 && !same ? 0 : i, 0, frx, csx, szx);
      Arr *b = take_cell(y, fry == 0 && !same ? 0 : i, 0, fry, csy, szy);
      Arr *v = fn_dya(f, a, b);
      a_rel(a); a_rel(b);
      a_put(r, i, v);
    }
  }
  r = a_simplify(r);
  res = disclose_p(r);
  a_rel(r);
  return a_simplify(res);
}

/* ---------- quad ---------- */
static Arr *quad_input(void)
{
  char buf[MAXLINE];
  size_t n;
  Arr *r;
  if (interactive) { fputs("      ", stdout); fflush(stdout); }
  if (!fgets(buf, sizeof(buf), stdin)) rt_error("[]: end of input");
  n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
  r = eval_text(buf);
  return need_val(r);
}

static Arr *quad_value(const char *name)
{
  if (strcmp(name, "IO") == 0) return a_scalar((double)Q_IO);
  if (strcmp(name, "PP") == 0) return a_scalar((double)Q_PP);
  if (strcmp(name, "CT") == 0) return a_scalar(Q_CT);
  if (strcmp(name, "RL") == 0) return a_scalar((double)Q_RL);
  if (strcmp(name, "A") == 0) return a_str("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  if (strcmp(name, "D") == 0) return a_str("0123456789");
  if (strcmp(name, "TS") == 0) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    Arr *r = a_vec(T_NUM, 6);
    r->num[0] = (double)(tm->tm_year + 1900);
    r->num[1] = (double)(tm->tm_mon + 1);
    r->num[2] = (double)tm->tm_mday;
    r->num[3] = (double)tm->tm_hour;
    r->num[4] = (double)tm->tm_min;
    r->num[5] = (double)tm->tm_sec;
    return r;
  }
  if (strcmp(name, "AV") == 0) {
    Arr *r = a_vec(T_CHR, 256);
    int i;
    for (i = 0; i < 256; i++) r->chr[i] = (char)i;
    return r;
  }
  if (strcmp(name, "TC") == 0) {
    Arr *r = a_vec(T_CHR, 3);
    r->chr[0] = (char)8; r->chr[1] = (char)13; r->chr[2] = (char)10;
    return r;
  }
  if (strcmp(name, "WA") == 0) return a_scalar((double)APL_MAXELEMS);
  if (strcmp(name, "AI") == 0) {
    Arr *r = a_vec(T_NUM, 4);
    double ms = floor((double)clock() * 1000.0 / (double)CLOCKS_PER_SEC);
    r->num[0] = 0.0; r->num[1] = ms; r->num[2] = ms; r->num[3] = 0.0;
    return r;
  }
  if (strcmp(name, "DM") == 0) return a_str(errmsg);
  if (strcmp(name, "EN") == 0) {
    static const struct { const char *pre; int code; } tab[] = {
      { "WS FULL", 1 }, { "RANK ERROR", 4 }, { "LENGTH ERROR", 5 },
      { "INDEX ERROR", 7 }, { "AXIS ERROR", 10 }, { "DOMAIN ERROR", 11 },
      { "DEPTH ERROR", 11 }, { NULL, 0 } };
    int i;
    if (errmsg[0] == '\0') return a_scalar(0.0);
    for (i = 0; tab[i].pre; i++)
      if (strncmp(errmsg, tab[i].pre, strlen(tab[i].pre)) == 0)
        return a_scalar((double)tab[i].code);
    if (strncmp(errkind, "syntax", 6) == 0) return a_scalar(2.0);
    return a_scalar(11.0);
  }
  syn_error_s("unknown system name: []", name);
  return NULL;
}

static char *text_of(Arr *a, const char *who)
{
  if (a->type != T_CHR || a->rank > 1)
    rt_error_s(who, " needs text operands", "");
  return xstrndup(a->chr, a->n);
}

/* []EA: execute alternate (try/catch) */
static Arr **sstk = NULL;
static int sstop = 0, sscap = 0;

static Arr *quad_ea(Arr *prim, Arr *alt)
{
  char *ps = text_of(prim, "[]EA");
  char *as = NULL;
  Arr *res = NULL;
  int depth = fdepth;
  int ssave = sstop;
  int tmark;
  trk_add(ps, 0);
  as = text_of(alt, "[]EA");
  trk_add(as, 0);
  tmark = ntrk - 2;
  if (catch_top + 1 >= MAXCATCH) rt_error("[]EA nested too deeply");
  catch_top++;
  if (setjmp(catch_jb[catch_top]) == 0) {
    res = eval_text(ps);
    catch_top--;
    trk_del(as); trk_del(ps);
    free(ps); free(as);
    return need_val(res);
  }
  catch_top--;
  sstop = ssave;
  unwind_frames(depth);
  br_set = 0;
  /* release temporaries made by the failed evaluation (keeps ps, as) */
  {
    int keep = tmark + 2;
    while (ntrk > keep) {
      Trk e = trk[--ntrk];
      if (e.is_tok) {
        Tok *tt = (Tok *)e.p;
        int k;
        for (k = 0; tt[k].k != K_END; k++) free(tt[k].s);
        free(tt);
      } else free(e.p);
    }
  }
  trk_del(ps);
  free(ps);
  res = eval_text(as);
  trk_del(as);
  free(as);
  return need_val(res);
}

/* ---------- name classification ---------- */
static int reserved_word(const char *s)
{
  return prim_lookup(s) != NULL || strcmp(s, "nabla") == 0 ||
         strcmp(s, "edit") == 0 || strcmp(s, "quad") == 0 ||
         strcmp(s, "lamp") == 0 || strcmp(s, "zilch") == 0 ||
         strcmp(s, "dop") == 0;
}

/* function reference starting at current token; advances */
static int try_fnref(Ps *p, FnRef *f)
{
  Tok *t = &p->t[p->i];
  const PrimDef *pd;
  if (tok_sym(t, "{")) {
    int nt;
    f->user = lambda_at(p, &nt);
    f->prim = -1; f->axis = -1; f->ntok = nt;
    p->i += nt;
    return 1;
  }
  if (t->k == K_SYM) {
    pd = prim_lookup(t->s);
    if (pd && !is_operator(pd->id)) {
      f->prim = pd->id; f->user = NULL; f->axis = -1; f->ntok = 1; p->i++;
      return 1;
    }
    return 0;
  }
  if (t->k == K_NAME) {
    Fn *u = fn_find(t->s);
    if (u && u->opn) return 0;
    if (u) { f->prim = -1; f->user = u; f->axis = -1; f->ntok = 1; p->i++; return 1; }
    pd = prim_lookup(t->s);
    if (pd && !is_operator(pd->id)) {
      f->prim = pd->id; f->user = NULL; f->axis = -1; f->ntok = 1; p->i++;
      return 1;
    }
  }
  return 0;
}

static void unexpected(const Tok *t)
{
  char buf[96];
  if (t->k == K_END) syn_error("unexpected end of statement");
  if (t->k == K_NUM) sprintf(buf, "unexpected number");
  else if (t->k == K_QUAD) sprintf(buf, "unexpected token near []");
  else sprintf(buf, "unexpected token near %.40s", t->s ? t->s : "?");
  syn_error(buf);
}

static Arr *parse_item0(Ps *p)
{
  Tok *t = &p->t[p->i];
  Arr *a;
  if (t->k == K_NUM) {
    p->i++;
    return a_scalar(t->num);
  }
  if (t->k == K_STR) {
    int n = (int)strlen(t->s);
    p->i++;
    if (n == 1) {
      a = a_new(T_CHR, 0, NULL);
      a->chr[0] = t->s[0];
      return a;
    }
    return a_str(t->s);
  }
  if (tok_sym(t, "(")) {
    p->i++;
    a = need_val(parse_expr(p));
    if (!tok_sym(&p->t[p->i], ")")) {
      a_rel(a);
      syn_error("missing closing parenthesis");
    }
    p->i++;
    return a;
  }
  if (t->k == K_QUAD || tok_name(t, "quad")) {
    p->i++;
    return quad_input();
  }
  if (t->k == K_QNAME) {
    p->i++;
    return quad_value(t->s);
  }
  if (t->k == K_NAME) {
    Fn *u = fn_find(t->s);
    Arr *v;
    if (u) {
      if (u->arity != 0) syn_error_s("missing argument for function: ", t->s);
      p->i++;
      return call_fn(u, NULL, NULL);
    }
    if (strcmp(t->s, "zilch") == 0) { p->i++; return a_vec(T_NUM, 0); }
    if (reserved_word(t->s)) unexpected(t);
    v = var_get(t->s);
    if (!v) syn_error_s("undefined variable: ", t->s);
    p->i++;
    return a_ref(v);
  }
  unexpected(t);
  return NULL;
}



/* optional [k] axis after a function; returns zero-based axis or -1 */
static int parse_axis_opt(Ps *p)
{
  Arr *a;
  int v;
  if (!tok_sym(&p->t[p->i], "[")) return -1;
  p->i++;
  a = need_val(parse_expr(p));
  if (!tok_sym(&p->t[p->i], "]")) {
    a_rel(a);
    syn_error("missing ] after axis");
  }
  p->i++;
  if (a->type != T_NUM || a->n != 1) {
    a_rel(a);
    rt_error("AXIS ERROR: axis must be a numeric scalar");
  }
  v = as_int(a->num[0], P_INDEX) - Q_IO;
  a_rel(a);
  if (v < 0) rt_error("AXIS ERROR: axis out of range");
  return v;
}

/* bracket list after an array: [i;j;...]; p->i is at the "[" */
static int parse_index_list(Ps *p, Arr **ix)
{
  int n = 0;
  p->i++;                                  /* [ */
  for (;;) {
    if (n >= MAXRANK) rt_error("RANK ERROR");
    if (tok_sym(&p->t[p->i], ";") || tok_sym(&p->t[p->i], "]")) ix[n] = NULL;
    else ix[n] = need_val(parse_expr(p));
    n++;
    if (tok_sym(&p->t[p->i], ";")) { p->i++; continue; }
    if (tok_sym(&p->t[p->i], "]")) { p->i++; break; }
    {
      int i;
      for (i = 0; i < n; i++) if (ix[i]) a_rel(ix[i]);
    }
    syn_error("missing ] in index");
  }
  return n;
}

static Arr *parse_item(Ps *p)
{
  Arr *a;
  if (p->t[p->i].k == K_NUM) {
    int n = 0, i;
    while (p->t[p->i + n].k == K_NUM) n++;
    if (n > 1 && tok_sym(&p->t[p->i + n], "[")) {
      a = a_vec(T_NUM, n);
      for (i = 0; i < n; i++) a->num[i] = p->t[p->i + i].num;
      p->i += n;
    } else a = parse_item0(p);
  } else a = parse_item0(p);
  while (tok_sym(&p->t[p->i], "[")) {
    Arr *ix[MAXRANK], *r;
    int n, i;
    n = parse_index_list(p, ix);
    r = select_p(a, ix, n);
    for (i = 0; i < n; i++) if (ix[i]) a_rel(ix[i]);
    a_rel(a);
    a = r;
  }
  return a;
}

/* does the token at p->i start another strand item? */
static int item_start(Ps *p)
{
  Tok *t = &p->t[p->i];
  if (t->k == K_NUM || t->k == K_STR) return 1;
  if (tok_sym(t, "(")) return 1;
  if (t->k == K_NAME) {
    if (strcmp(t->s, "zilch") == 0) return 1;
    return !fn_find(t->s) && !reserved_word(t->s);
  }
  return 0;
}

static Arr *build_strand(Arr **it, int n)
{
  Arr *r;
  int i, allnum = 1, allchr = 1;
  if (n == 1) { r = it[0]; return r; }
  for (i = 0; i < n; i++) {
    if (!(it[i]->type == T_NUM && it[i]->rank == 0)) allnum = 0;
    if (!(it[i]->type == T_CHR && it[i]->rank == 0)) allchr = 0;
  }
  if (allnum) {
    r = a_vec(T_NUM, n);
    for (i = 0; i < n; i++) { r->num[i] = it[i]->num[0]; a_rel(it[i]); }
  } else if (allchr) {
    r = a_vec(T_CHR, n);
    for (i = 0; i < n; i++) { r->chr[i] = it[i]->chr[0]; a_rel(it[i]); }
  } else {
    r = a_vec(T_NEST, n);
    for (i = 0; i < n; i++) a_put(r, i, it[i]);
  }
  return r;
}

/* strand items are collected on a global stack so that an error raised
 * while parsing a later item cannot leak a malloc'd buffer */

static Arr *parse_operand(Ps *p)
{
  int base = sstop, n;
  Arr *r;
  for (;;) {
    Arr *a = parse_item(p);
    if (sstop == sscap) {
      sscap = sscap ? sscap * 2 : 64;
      sstk = (Arr **)xrealloc(sstk, sizeof(Arr *) * (size_t)sscap);
    }
    sstk[sstop++] = a;
    if (!item_start(p)) break;
  }
  n = sstop - base;
  r = build_strand(sstk + base, n);
  sstop = base;
  return r;
}

/* x keyed f y : group by key; f is called with each key and its indices
 * (monadic form) or with each key and the matching rows of y (dyadic) */
static Arr *op_key(const FnRef *f, Arr *left, Arr *y)
{
  Arr *k = left ? left : y, *res, *out;
  int n, i, g, nk = 0, *grp, *firstat;
  if (k->rank > 1) rt_error("RANK ERROR: keyed needs a vector of keys");
  n = k->rank == 0 ? 1 : k->n;
  if (left && (y->rank == 0 ? 1 : y->shape[0]) != n)
    rt_error("LENGTH ERROR: keyed: keys and data differ in length");
  grp = (int *)xmalloc(sizeof(int) * (size_t)(n + 1));
  firstat = (int *)xmalloc(sizeof(int) * (size_t)(n + 1));
  for (i = 0; i < n; i++) {
    Arr *a = a_item(k, i);
    int found = -1;
    for (g = 0; g < nk && found < 0; g++) {
      Arr *b = a_item(k, firstat[g]);
      Arr *m = match_p(a, b);
      if (m->num[0] != 0.0) found = g;
      a_rel(m); a_rel(b);
    }
    a_rel(a);
    if (found < 0) { firstat[nk] = i; found = nk++; }
    grp[i] = found;
  }
  res = a_new(T_NEST, 1, &nk);
  for (g = 0; g < nk; g++) {
    Arr *key = a_item(k, firstat[g]), *arg, *r;
    if (left) {
      Arr *mask = a_vec(T_NUM, n);
      for (i = 0; i < n; i++) mask->num[i] = (grp[i] == g) ? 1.0 : 0.0;
      arg = compress_p(mask, y, 0);
      a_rel(mask);
    } else {
      int c = 0;
      for (i = 0; i < n; i++) if (grp[i] == g) c++;
      arg = a_vec(T_NUM, c);
      for (i = 0, c = 0; i < n; i++)
        if (grp[i] == g) arg->num[c++] = (double)(i + Q_IO);
    }
    r = fn_dya(f, key, arg);
    a_rel(key); a_rel(arg);
    a_put(res, g, r);
  }
  free(grp); free(firstat);
  out = a_simplify(res);
  if (out->type == T_NEST) {
    Arr *d = disclose_p(out);
    a_rel(out);
    out = a_simplify(d);
  }
  return out;
}

/* amend idx f y   or   v amend idx y */
static Arr *op_amend(Ps *p, Arr *left)
{
  FnRef f;
  Arr *idx = parse_item(p), *y, *val, *ix[1], *r;
  if (!left && !try_fnref(p, &f)) {
    a_rel(idx);
    syn_error("amend needs a function or a left argument");
  }
  y = need_val(parse_expr(p));
  if (y->rank != 1) { a_rel(idx); a_rel(y); rt_error("RANK ERROR: amend works on vectors"); }
  if (left) val = a_ref(left);
  else {
    Arr *sel = index_p(y, idx);
    val = fn_mon(&f, sel);
    a_rel(sel);
  }
  ix[0] = idx;
  r = assign_index_p(y, ix, 1, val);
  a_rel(idx); a_rel(y); a_rel(val);
  return r;
}

/* p->i is just after the operator word; left is its left argument or NULL */
static Arr *apply_operator(Ps *p, int id, Arr *left)
{
  FnRef f, g;
  Arr *a, *b, *r;
  const char *nm = prim_name(id);
  if (id == P_AMEND) return op_amend(p, left);
  if (!try_fnref(p, &f)) syn_error_s("missing function after ", nm);
  f.axis = parse_axis_opt(p);
  switch (id) {
  case P_OUTER:
    if (left) {
      b = need_val(parse_expr(p));
      r = op_outer(&f, left, b);
      a_rel(b);
      return r;
    }
    a = parse_item(p);
    b = parse_item(p);
    r = op_outer(&f, a, b);
    a_rel(a); a_rel(b);
    return r;
  case P_INNER:
    if (!left) syn_error("inner needs a left argument");
    if (!try_fnref(p, &g)) syn_error("inner needs two functions");
    b = need_val(parse_expr(p));
    r = op_inner(&f, &g, left, b);
    a_rel(b);
    return r;
  case P_SWAP:
    a = need_val(parse_expr(p));
    r = left ? fn_dya(&f, a, left) : fn_dya(&f, a, a);
    a_rel(a);
    return r;
  case P_REPEAT: {
    int n, i;
    Arr *cnt = parse_item(p), *v;
    need_num(cnt, P_REPEAT);
    if (cnt->n != 1) { a_rel(cnt); rt_error("DOMAIN ERROR: repeat count must be a scalar"); }
    n = as_int(cnt->num[0], P_REPEAT);
    a_rel(cnt);
    if (n < 0) rt_error("DOMAIN ERROR: repeat count must not be negative");
    v = need_val(parse_expr(p));
    for (i = 0; i < n; i++) {
      Arr *nv = left ? fn_dya(&f, left, v) : fn_mon(&f, v);
      a_rel(v);
      v = nv;
    }
    return v;
  }
  case P_UNTIL: {
    int n;
    Arr *v;
    if (!try_fnref(p, &g)) syn_error("until needs a test function");
    v = need_val(parse_expr(p));
    for (n = 0; n < 1000000; n++) {
      Arr *nv = left ? fn_dya(&f, left, v) : fn_mon(&f, v);
      Arr *t = fn_dya(&g, nv, v);
      int done;
      if (t->type != T_NUM || t->n != 1) {
        a_rel(t); a_rel(nv); a_rel(v);
        rt_error("DOMAIN ERROR: until test must give a numeric scalar");
      }
      done = (t->num[0] != 0.0);
      a_rel(t); a_rel(v);
      v = nv;
      if (done) return v;
    }
    a_rel(v);
    rt_error("until: no convergence after 1000000 steps");
    return NULL;
  }
  case P_OVER: case P_ATOP: {
    Arr *y;
    if (!try_fnref(p, &g)) syn_error_s("missing second function after ", nm);
    y = need_val(parse_expr(p));
    if (id == P_OVER) {
      if (left) {
        Arr *gx = fn_mon(&g, left), *gy = fn_mon(&g, y);
        r = fn_dya(&f, gx, gy);
        a_rel(gx); a_rel(gy);
      } else {
        Arr *gy = fn_mon(&g, y);
        r = fn_mon(&f, gy);
        a_rel(gy);
      }
    } else {
      Arr *gy = left ? fn_dya(&g, left, y) : fn_mon(&g, y);
      r = fn_mon(&f, gy);
      a_rel(gy);
    }
    a_rel(y);
    return r;
  }
  case P_RANK: {
    Arr *k = parse_item(p);
    b = need_val(parse_expr(p));
    r = left ? op_rank(&f, k, left, b) : op_rank(&f, k, b, NULL);
    a_rel(k); a_rel(b);
    return r;
  }
  default:
    break;
  }
  a = need_val(parse_expr(p));
  if (id == P_KEYED) { r = op_key(&f, left, a); a_rel(a); return r; }
  if (id == P_REDUCE) r = left ? op_nreduce(&f, left, a, f.axis)
                               : op_reduce(&f, a, f.axis);
  else if (id == P_SCAN) {
    if (left) syn_error("scan has no dyadic form");
    r = op_scan(&f, a, f.axis);
  } else r = left ? op_each2(&f, left, a) : op_each(&f, a);
  a_rel(a);
  return r;
}

static Arr *parse_operator(Ps *p)
{
  int id = prim_lookup(p->t[p->i].s)->id;
  p->i++;
  return apply_operator(p, id, NULL);
}

/* Does the token at p->i start a dyadic function? fills f / sets ea */
static int dyadic_at(Ps *p, FnRef *f, int *ea)
{
  Tok *t = &p->t[p->i];
  *ea = 0;
  if (t->k == K_QNAME && strcmp(t->s, "EA") == 0) { *ea = 1; f->ntok = 1; return 1; }
  if (t->k == K_QNAME && strcmp(t->s, "SVO") == 0) { *ea = 2; f->ntok = 1; return 1; }
  if (tok_sym(t, "{")) {
    int nt;
    Fn *u = lambda_at(p, &nt);
    if (u->arity != 2) return 0;
    f->prim = -1; f->user = u; f->axis = -1; f->ntok = nt;
    return 1;
  }
  if (t->k == K_SYM) {
    const PrimDef *pd = prim_lookup(t->s);
    if (pd && pd->dya && !is_operator(pd->id)) {
      f->prim = pd->id; f->user = NULL; f->axis = -1; f->ntok = 1;
      return 1;
    }
    return 0;
  }
  if (t->k == K_NAME) {
    Fn *u = fn_find(t->s);
    if (u) {
      if (u->opn || u->arity != 2) return 0;
      f->prim = -1; f->user = u; f->axis = -1; f->ntok = 1;
      return 1;
    }
    {
      const PrimDef *pd = prim_lookup(t->s);
      if (pd && pd->dya && !is_operator(pd->id)) {
        f->prim = pd->id; f->user = NULL; f->axis = -1; f->ntok = 1;
        return 1;
      }
    }
  }
  return 0;
}

static Arr *parse_expr(Ps *p)
{
  Tok *t = &p->t[p->i];
  Arr *a, *r, *res;
  FnRef f;
  int ea;
  /* leading function => monadic application */
  if (t->k == K_SYM && (strcmp(t->s, "+") == 0 || strcmp(t->s, "-") == 0)) {
    int id = (t->s[0] == '+') ? P_ADD : P_SUB;
    p->i++;
    r = need_val(parse_expr(p));
    res = prim_mon(id, r);
    a_rel(r);
    return res;
  }
  if (t->k == K_QNAME && quad_is_fn(t->s)) {
    const char *qn = t->s;
    p->i++;
    r = need_val(parse_expr(p));
    res = quad_fn(qn, r);
    a_rel(r);
    return res;
  }
  if (tok_sym(t, "{")) {
    int nt;
    Fn *u = lambda_at(p, &nt);
    p->i += nt;
    if (u->arity == 1 || u->ambi) {
      r = need_val(parse_expr(p));
      res = call_fn(u, NULL, r);
      a_rel(r);
      return res;
    }
    syn_error("lambda needs a left argument");
  }
  if (t->k == K_NAME) {
    Fn *u = fn_find(t->s);
    if (u && u->opn) { p->i++; return apply_userop(p, u, NULL); }
    if (u) {
      if (u->arity == 1 || u->ambi) {
        p->i++;
        r = need_val(parse_expr(p));
        res = call_fn(u, NULL, r);
        a_rel(r);
        return res;
      }
      if (u->arity == 2) syn_error_s("missing left argument for ", t->s);
    } else {
      const PrimDef *pd = prim_lookup(t->s);
      if (pd) {
        if (is_operator(pd->id)) return parse_operator(p);
        if (pd->mon) {
          int ax;
          p->i++;
          ax = parse_axis_opt(p);
          r = need_val(parse_expr(p));
          cur_axis = ax;
          res = prim_mon(pd->id, r);
          cur_axis = -1;
          a_rel(r);
          return res;
        }
        syn_error_s("missing left argument for ", t->s);
      }
    }
  }
  a = parse_operand(p);
  if (a && p->t[p->i].k == K_NAME) {
    Fn *uo = fn_find(p->t[p->i].s);
    const PrimDef *pd;
    if (uo && uo->opn) {
      p->i++;
      res = apply_userop(p, uo, a);
      a_rel(a);
      return res;
    }
    pd = prim_lookup(p->t[p->i].s);
    if (pd && is_operator(pd->id)) {
      p->i++;
      res = apply_operator(p, pd->id, a);
      a_rel(a);
      return res;
    }
  }
  if (dyadic_at(p, &f, &ea)) {
    if (!a) syn_error("function returned no value");
    p->i += f.ntok;
    f.axis = ea ? -1 : parse_axis_opt(p);
    r = need_val(parse_expr(p));
    if (ea == 2) res = quad_svo(a, r);
    else if (ea) res = quad_ea(a, r);
    else {
      if (!f.user) cur_axis = f.axis;
      res = fn_dya(&f, a, r);
      cur_axis = -1;
    }
    a_rel(a);
    a_rel(r);
    return res;
  }
  return a;
}

static Arr *eval_text(const char *s)
{
  Tok *t = lex_t(s);
  Ps p;
  Arr *a;
  p.t = t; p.i = 0;
  a = parse_expr(&p);
  if (p.t[p.i].k != K_END) {
    Tok *bad = &p.t[p.i];
    if (a) a_rel(a);
    unexpected(bad);
  }
  lex_free(t);
  return a;
}

/* ---------- statements ---------- */
static void assign_check(const char *name)
{
  if (reserved_word(name))
    syn_error_s("cannot assign to reserved word: ", name);
  if (fn_find(name))
    syn_error_s("name is already a function: ", name);
}

static void exec_tokens(Tok *t)
{
  Ps p;
  Arr *a;
  if (t[0].k == K_END) return;
  p.t = t; p.i = 0;
  if (tok_sym(&t[0], "->")) {
    if (fdepth == 0) {
      printf("apl: -> (branch) can only be used inside a function body\n");
      return;
    }
    p.i = 1;
    if (t[1].k == K_END) { br_set = 1; br_target = 0; return; }
    a = need_val(parse_expr(&p));
    if (p.t[p.i].k != K_END) unexpected(&p.t[p.i]);
    if (a->type != T_NUM) { a_rel(a); rt_error("branch target must be numeric"); }
    if (a->n > 0) { br_set = 1; br_target = as_int(a->num[0], P_INDEX); }
    a_rel(a);
    return;
  }
  if (t[0].k == K_NAME && tok_sym(&t[1], "<-") && tok_name(&t[2], "dop") &&
      tok_sym(&t[3], "{") && t[brace_end(t, 3) + 1].k == K_END) {
    Fn *ex = fn_find(t[0].s), *nf;
    int e = brace_end(t, 3), i, a1 = 0, a2 = 0;
    char *body;
    if (reserved_word(t[0].s))
      syn_error_s("cannot assign to reserved word: ", t[0].s);
    if (ex && !ex->lam) syn_error_s("name is already a function: ", t[0].s);
    for (i = 4; i < e; i++) {
      if (tok_name(&t[i], "alphaalpha")) a1 = 1;
      if (tok_name(&t[i], "omegaomega")) a2 = 1;
    }
    if (!a1 && !a2) syn_error("a dop body must use alphaalpha (and optionally omegaomega)");
    if (a2 && !a1) syn_error("a dop that uses omegaomega must also use alphaalpha");
    nf = lambda_make(t, 4, e, t[0].s);
    nf->opn = a2 ? 2 : 1;
    nf->optok = tok_slice(t, 4, e);
    body = tok_src(t, 4, e);
    free(nf->hdr);
    nf->hdr = (char *)xmalloc(strlen(body) + 12);
    sprintf(nf->hdr, "dop { %s }", body);
    free(body);
    var_set(t[0].s, NULL);
    fn_install(nf);
    return;
  }
  if (t[0].k == K_NAME && tok_sym(&t[1], "<-") && tok_sym(&t[2], "{") &&
      t[brace_end(t, 2) + 1].k == K_END) {
    Fn *ex = fn_find(t[0].s), *nf;
    if (reserved_word(t[0].s))
      syn_error_s("cannot assign to reserved word: ", t[0].s);
    if (ex && !ex->lam) syn_error_s("name is already a function: ", t[0].s);
    nf = lambda_make(t, 3, brace_end(t, 2), t[0].s);
    var_set(t[0].s, NULL);
    fn_install(nf);
    return;
  }
  if (t[0].k == K_NAME && tok_sym(&t[1], "[")) {
    int d = 0, j = 1;
    while (t[j].k != K_END) {
      if (tok_sym(&t[j], "[")) d++;
      else if (tok_sym(&t[j], "]") && --d == 0) break;
      j++;
    }
    if (t[j].k != K_END && tok_sym(&t[j + 1], "<-")) {
      Arr *ix[MAXRANK], *val, *cur, *res;
      int n, i;
      assign_check(t[0].s);
      cur = var_get(t[0].s);
      if (!cur) syn_error_s("undefined variable: ", t[0].s);
      p.i = 1;
      n = parse_index_list(&p, ix);
      p.i++;                                  /* <- */
      val = need_val(parse_expr(&p));
      if (p.t[p.i].k != K_END) {
        a_rel(val);
        for (i = 0; i < n; i++) if (ix[i]) a_rel(ix[i]);
        unexpected(&p.t[p.i]);
      }
      cur = var_get(t[0].s);
      res = assign_index_p(cur, ix, n, val);
      for (i = 0; i < n; i++) if (ix[i]) a_rel(ix[i]);
      a_rel(val);
      var_set(t[0].s, res);
      return;
    }
  }
  if (tok_sym(&t[1], "<-") &&
      (t[0].k == K_NAME || t[0].k == K_QNAME || t[0].k == K_QUAD)) {
    p.i = 2;
    a = need_val(parse_expr(&p));
    if (p.t[p.i].k != K_END) { a_rel(a); unexpected(&p.t[p.i]); }
    if (t[0].k == K_QUAD || tok_name(&t[0], "quad")) {
      show(a);
      a_rel(a);
    } else if (t[0].k == K_QNAME) {
      if (a->type != T_NUM || a->n != 1) {
        a_rel(a);
        rt_error("system variable needs a numeric scalar");
      }
      if (strcmp(t[0].s, "IO") == 0) {
        int v = as_int(a->num[0], P_INDEX);
        if (v != 0 && v != 1) { a_rel(a); rt_error("[]IO must be 0 or 1"); }
        Q_IO = v;
      } else if (strcmp(t[0].s, "CT") == 0) {
        if (a->num[0] < 0.0 || a->num[0] > 1.0) { a_rel(a); rt_error("[]CT must be between 0 and 1"); }
        Q_CT = a->num[0];
      } else if (strcmp(t[0].s, "RL") == 0) {
        double v = a->num[0];
        if (v < 1.0 || v >= 4294967296.0 || v != floor(v)) { a_rel(a); rt_error("[]RL must be an integer from 1 to 4294967295"); }
        Q_RL = (unsigned long)v;
        { int w; for (w = 0; w < 16; w++) rnd_next(); }
      } else if (strcmp(t[0].s, "PP") == 0) {
        int v = as_int(a->num[0], P_INDEX);
        if (v < 0 || v > 17) { a_rel(a); rt_error("[]PP must be 0..17"); }
        Q_PP = v;
      } else {
        a_rel(a);
        rt_error_s("[]", t[0].s, " cannot be assigned");
      }
      a_rel(a);
    } else {
      assign_check(t[0].s);
      var_set(t[0].s, a);
    }
    return;
  }
  a = parse_expr(&p);
  if (p.t[p.i].k != K_END) {
    Tok *bad = &p.t[p.i];
    if (a) a_rel(a);
    unexpected(bad);
  }
  if (a) { show(a); a_rel(a); }
}

/* ---------- calling user functions ---------- */
static void frame_save(Frame *fr, const char *name)
{
  int i;
  for (i = 0; i < fr->nsaved; i++)
    if (strcmp(fr->sv[i].name, name) == 0) return;
  fr->sv = (Saved *)xrealloc(fr->sv, sizeof(Saved) * (size_t)(fr->nsaved + 1));
  fr->sv[fr->nsaved].name = name;
  fr->sv[fr->nsaved].val = var_take(name);
  fr->nsaved++;
}

/* is this statement an assignment (so exec_tokens, not a value)? */
static int is_assignment(const Tok *t)
{
  int d = 0, j = 1;
  if (t[0].k == K_END) return 0;
  if ((t[0].k == K_NAME || t[0].k == K_QNAME || t[0].k == K_QUAD) &&
      tok_sym(&t[1], "<-")) return 1;
  if (t[0].k == K_NAME && tok_sym(&t[1], "[")) {
    for (; t[j].k != K_END; j++) {
      if (tok_sym(&t[j], "[")) d++;
      else if (tok_sym(&t[j], "]") && --d == 0) break;
    }
    return t[j].k != K_END && tok_sym(&t[j + 1], "<-");
  }
  return 0;
}

/* run text as a statement: value, or NULL for an assignment / no value */
static Arr *exec_text(const char *s)
{
  Tok *t = lex_t(s);
  Arr *r;
  if (is_assignment(t)) {
    exec_tokens(t);
    lex_free(t);
    return NULL;
  }
  lex_free(t);
  r = eval_text(s);
  return r;
}

/* ---------- lambdas: { statements } with alpha and omega ---------- */
static Fn **lamtab = NULL;
static int nlam = 0;

static Tok *tok_slice(const Tok *t, int a, int b)       /* [a, b) */
{
  int n = b - a, i;
  Tok *r = (Tok *)xmalloc(sizeof(Tok) * (size_t)(n + 1));
  for (i = 0; i < n; i++) {
    r[i].k = t[a + i].k;
    r[i].num = t[a + i].num;
    r[i].s = t[a + i].s ? xstrdup(t[a + i].s) : NULL;
  }
  r[n].k = K_END; r[n].num = 0; r[n].s = NULL;
  return r;
}

/* rebuild source text for tokens [a, b) */
static char *tok_src(const Tok *t, int a, int b)
{
  size_t cap = 64, len = 0;
  char *out = (char *)xmalloc(cap);
  int i;
  out[0] = '\0';
  for (i = a; i < b; i++) {
    char buf[64];
    const char *piece = buf;
    char *tmp = NULL;
    size_t pl;
    switch (t[i].k) {
    case K_NUM: sprintf(buf, "%.17g", t[i].num); break;
    case K_NAME: case K_SYM: piece = t[i].s; break;
    case K_QUAD: piece = "[]"; break;
    case K_QNAME:
      tmp = (char *)xmalloc(strlen(t[i].s) + 3);
      sprintf(tmp, "[]%s", t[i].s);
      piece = tmp;
      break;
    case K_STR: {
      const char *s = t[i].s;
      char *w = tmp = (char *)xmalloc(strlen(s) * 2 + 3);
      *w++ = '\'';
      for (; *s; s++) { if (*s == '\'') *w++ = '\''; *w++ = *s; }
      *w++ = '\''; *w = '\0';
      piece = tmp;
      break;
    }
    default: piece = "?"; break;
    }
    pl = strlen(piece);
    while (len + pl + 2 > cap) { cap *= 2; out = (char *)xrealloc(out, cap); }
    if (len) out[len++] = ' ';
    memcpy(out + len, piece, pl);
    len += pl;
    out[len] = '\0';
    free(tmp);
  }
  return out;
}

/* t[i] is "{": index of the matching "}" */
static int brace_end(const Tok *t, int i)
{
  int d = 0;
  for (; t[i].k != K_END; i++) {
    if (tok_sym(&t[i], "{")) d++;
    else if (tok_sym(&t[i], "}") && --d == 0) return i;
  }
  syn_error("missing } in lambda");
  return i;
}

static void lam_addloc(Fn *f, const char *s)
{
  int i;
  for (i = 0; i < f->nloc; i++) if (strcmp(f->loc[i], s) == 0) return;
  f->loc = (char **)xrealloc(f->loc, sizeof(char *) * (size_t)(f->nloc + 1));
  f->loc[f->nloc++] = xstrdup(s);
}

/* guard colon at nesting depth 0 of one statement, or -1 */
static int stmt_colon(const Tok *t)
{
  int i, d = 0;
  for (i = 0; t[i].k != K_END; i++) {
    if (tok_sym(&t[i], "(") || tok_sym(&t[i], "[") || tok_sym(&t[i], "{")) d++;
    else if (tok_sym(&t[i], ")") || tok_sym(&t[i], "]") || tok_sym(&t[i], "}")) d--;
    else if (d == 0 && tok_sym(&t[i], ":")) return i;
  }
  return -1;
}

/* build a lambda from the tokens strictly between the braces [a, b) */
static Fn *lambda_make(const Tok *t, int a, int b, const char *name)
{
  Fn *f = (Fn *)xmalloc(sizeof(Fn));
  int i, st = a, d = 0, uses_alpha = 0;
  char *body = tok_src(t, a, b);
  f->name = xstrdup(name);
  f->res = f->left = f->right = NULL;
  f->hdr = (char *)xmalloc(strlen(body) + 5);
  sprintf(f->hdr, "{ %s }", body);
  free(body);
  f->nloc = 0; f->loc = NULL;
  f->nlines = 0; f->lines = NULL; f->tk = NULL; f->label = NULL;
  f->lam = 1; f->nstmt = 0; f->stk = NULL;
  f->cbuilt = 1; f->hasctl = 0; f->ck = NULL; f->cj = NULL;
  f->opn = 0; f->optok = NULL;
  lam_addloc(f, "alpha");
  lam_addloc(f, "omega");
  for (i = a; i <= b; i++) {
    int end = (i == b);
    if (!end) {
      if (tok_sym(&t[i], "(") || tok_sym(&t[i], "[") || tok_sym(&t[i], "{")) d++;
      else if (tok_sym(&t[i], ")") || tok_sym(&t[i], "]") || tok_sym(&t[i], "}")) d--;
      if (tok_name(&t[i], "alpha")) uses_alpha = 1;
    }
    if (end || (d == 0 && tok_sym(&t[i], ";"))) {
      if (i > st) {
        Tok *s = tok_slice(t, st, i);
        int c = stmt_colon(s), k = (c >= 0) ? c + 1 : 0;
        f->stk = (Tok **)xrealloc(f->stk, sizeof(Tok *) * (size_t)(f->nstmt + 1));
        f->stk[f->nstmt++] = s;
        if (s[k].k == K_NAME && tok_sym(&s[k + 1], "<-")) lam_addloc(f, s[k].s);
      }
      st = i + 1;
    }
  }
  f->arity = uses_alpha ? 2 : 1;
  f->ambi = uses_alpha;
  return f;
}

/* t[p->i] is "{": fetch (or build) the lambda; *ntok = tokens it spans */
static Fn *lambda_at(Ps *p, int *ntok)
{
  int j = brace_end(p->t, p->i), i;
  char *body = tok_src(p->t, p->i + 1, j);
  char *key = (char *)xmalloc(strlen(body) + 5);
  Fn *f;
  sprintf(key, "{ %s }", body);
  free(body);
  *ntok = j - p->i + 1;
  for (i = 0; i < nlam; i++)
    if (strcmp(lamtab[i]->hdr, key) == 0) { free(key); return lamtab[i]; }
  free(key);
  f = lambda_make(p->t, p->i + 1, j, "{}");
  lamtab = (Fn **)xrealloc(lamtab, sizeof(Fn *) * (size_t)(nlam + 1));
  lamtab[nlam++] = f;
  return f;
}

/* operator defined by the user with  name <- dop { ... alphaalpha ... } :
 * the operand functions are spliced into the body, which is then used as an
 * ordinary lambda (cached by its text) */
static Arr *apply_userop(Ps *p, Fn *u, Arr *left)
{
  FnRef f, g;
  int fs, fe, gs = 0, ge = 0, i, n = 0, nt;
  Tok *arr, *q;
  Fn *lam;
  Ps ps;
  Arr *y, *r;
  fs = p->i;
  if (!try_fnref(p, &f)) syn_error_s("missing function after ", u->name);
  fe = p->i;
  if (u->opn == 2) {
    gs = p->i;
    if (!try_fnref(p, &g)) syn_error_s("missing second function after ", u->name);
    ge = p->i;
  }
  for (i = 0; u->optok[i].k != K_END; i++) {
    if (tok_name(&u->optok[i], "alphaalpha")) n += fe - fs;
    else if (tok_name(&u->optok[i], "omegaomega")) n += ge - gs;
    else n++;
  }
  arr = (Tok *)xmalloc(sizeof(Tok) * (size_t)(n + 4));
  q = arr;
  q->k = K_SYM; q->num = 0; q->s = xstrdup("{"); q++;
  for (i = 0; u->optok[i].k != K_END; i++) {
    int a = -1, b = -1, j;
    if (tok_name(&u->optok[i], "alphaalpha")) { a = fs; b = fe; }
    else if (tok_name(&u->optok[i], "omegaomega")) { a = gs; b = ge; }
    if (a >= 0) {
      for (j = a; j < b; j++) {
        q->k = p->t[j].k; q->num = p->t[j].num;
        q->s = p->t[j].s ? xstrdup(p->t[j].s) : NULL; q++;
      }
    } else {
      q->k = u->optok[i].k; q->num = u->optok[i].num;
      q->s = u->optok[i].s ? xstrdup(u->optok[i].s) : NULL; q++;
    }
  }
  q->k = K_SYM; q->num = 0; q->s = xstrdup("}"); q++;
  q->k = K_END; q->num = 0; q->s = NULL;
  trk_add(arr, 1);
  ps.t = arr; ps.i = 0;
  lam = lambda_at(&ps, &nt);
  trk_del(arr);
  lex_free(arr);
  y = need_val(parse_expr(p));
  r = need_val(call_fn(lam, left, y));
  a_rel(y);
  return r;
}

static Arr *call_lambda(Fn *f, Arr *left, Arr *right)
{
  Frame *fr;
  int i;
  Arr *result = NULL;
  if (fdepth >= MAXDEPTH) rt_error("DEPTH ERROR: calls nested too deeply");
  fr = &frames[fdepth++];
  fr->fn = f; fr->nsaved = 0; fr->sv = NULL;
  for (i = 0; i < f->nloc; i++) frame_save(fr, f->loc[i]);
  if (left) var_set("alpha", a_ref(left));
  var_set("omega", a_ref(right));
  for (i = 0; i < f->nstmt && !result; i++) {
    Tok *t = f->stk[i];
    Ps p;
    Arr *v;
    int c = stmt_colon(t);
    p.t = t; p.i = 0;
    if (c >= 0) {
      Arr *g = need_val(parse_expr(&p));
      int ok;
      if (!tok_sym(&p.t[p.i], ":")) { a_rel(g); unexpected(&p.t[p.i]); }
      if (g->type != T_NUM || g->n != 1) {
        a_rel(g);
        rt_error("DOMAIN ERROR: lambda guard must be a numeric scalar");
      }
      ok = (g->num[0] != 0.0);
      a_rel(g);
      if (!ok) continue;
      p.i++;
      v = parse_expr(&p);
      if (p.t[p.i].k != K_END) { if (v) a_rel(v); unexpected(&p.t[p.i]); }
      result = need_val(v);
      continue;
    }
    if (is_assignment(t)) {
      exec_tokens(t);
      continue;
    }
    v = parse_expr(&p);
    if (p.t[p.i].k != K_END) { if (v) a_rel(v); unexpected(&p.t[p.i]); }
    if (v) result = v;
  }
  unwind_frames(fdepth - 1);
  return result;
}

/* ---------- structured control: :If :While :For :Select ... ---------- */
enum { C_NONE, C_IF, C_ELSEIF, C_ELSE, C_ENDIF, C_WHILE, C_ENDWHILE,
  C_REPEAT, C_UNTIL, C_FOR, C_ENDFOR, C_SELECT, C_CASE, C_CASELIST,
  C_ENDSELECT, C_LEAVE, C_CONTINUE, C_RETURN };

static const char *CTLW[] = {
  "", "if", "elseif", "else", "endif", "while", "endwhile", "repeat",
  "until", "for", "endfor", "select", "case", "caselist", "endselect",
  "leave", "continue", "return", NULL };

static int ctl_kind(const char *line, const char **rest)
{
  const char *p = line;
  char w[16];
  int n = 0, i;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != ':') return C_NONE;
  p++;
  while (is_alpha((unsigned char)*p) && n < 15) {
    w[n++] = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    p++;
  }
  w[n] = '\0';
  for (i = 1; CTLW[i]; i++)
    if (strcmp(w, CTLW[i]) == 0) { if (rest) *rest = p; return i; }
  return C_NONE;
}

#define CTLDEPTH 32
typedef struct CEnt {
  int kind, line, ncl, nbr;
  int cl[CTLDEPTH];
  int br[CTLDEPTH];
} CEnt;

static void ctl_build(Fn *f)
{
  CEnt st[CTLDEPTH];
  int sp = 0, i, k, j;
  f->cbuilt = 1;
  f->hasctl = 0;
  f->ck = (int *)xmalloc(sizeof(int) * (size_t)(f->nlines + 2));
  f->cj = (int *)xmalloc(sizeof(int) * (size_t)(3 * (f->nlines + 2)));
  for (i = 0; i < 3 * (f->nlines + 2); i++) f->cj[i] = 0;
  for (i = 0; i <= f->nlines + 1; i++) f->ck[i] = C_NONE;
  for (i = 1; i <= f->nlines; i++) {
    k = ctl_kind(f->lines[i], NULL);
    f->ck[i] = k;
    if (k == C_NONE) continue;
    f->hasctl = 1;
    switch (k) {
    case C_IF: case C_SELECT: case C_WHILE: case C_REPEAT: case C_FOR:
      if (sp >= CTLDEPTH) syn_error("control structures nested too deeply");
      st[sp].kind = k; st[sp].line = i; st[sp].ncl = 1; st[sp].nbr = 0;
      st[sp].cl[0] = i; sp++;
      break;
    case C_ELSEIF: case C_ELSE: case C_CASE: case C_CASELIST:
      if (sp == 0 || (st[sp - 1].kind != C_IF && st[sp - 1].kind != C_SELECT))
        syn_error("control word outside :If / :Select");
      if (st[sp - 1].ncl >= CTLDEPTH) syn_error("too many clauses");
      st[sp - 1].cl[st[sp - 1].ncl++] = i;
      break;
    case C_ENDIF: case C_ENDSELECT:
      if (sp == 0 || st[sp - 1].kind != (k == C_ENDIF ? C_IF : C_SELECT))
        syn_error("unbalanced :End word");
      sp--;
      for (j = 0; j < st[sp].ncl; j++) {
        int ln = st[sp].cl[j];
        f->cj[3 * ln] = (j + 1 < st[sp].ncl) ? st[sp].cl[j + 1] : i;
        f->cj[3 * ln + 1] = i;
      }
      break;
    case C_ENDWHILE: case C_UNTIL: case C_ENDFOR: {
      int want = (k == C_ENDWHILE) ? C_WHILE : (k == C_UNTIL ? C_REPEAT : C_FOR);
      int lp, after = i + 1;
      if (sp == 0 || st[sp - 1].kind != want) syn_error("unbalanced :End word");
      sp--;
      lp = st[sp].line;
      if (k == C_UNTIL) f->cj[3 * i] = lp + 1;
      else { f->cj[3 * i] = lp; f->cj[3 * lp] = after; }
      for (j = 0; j < st[sp].nbr; j++) {
        int b = st[sp].br[j];
        f->cj[3 * b] = (f->ck[b] == C_LEAVE) ? after : i;
        f->cj[3 * b + 2] = lp;
      }
      break;
    }
    case C_LEAVE: case C_CONTINUE: {
      int q;
      for (q = sp - 1; q >= 0; q--)
        if (st[q].kind == C_WHILE || st[q].kind == C_REPEAT || st[q].kind == C_FOR) break;
      if (q < 0) syn_error(":Leave / :Continue outside a loop");
      if (st[q].nbr >= CTLDEPTH) syn_error("too many :Leave / :Continue");
      st[q].br[st[q].nbr++] = i;
      break;
    }
    default: break;
    }
  }
  if (sp != 0) syn_error("missing :End word in function");
}

typedef struct FSt { int line; Arr *vec; int idx; char name[64]; } FSt;
typedef struct CSt {
  FSt fs[CTLDEPTH]; int nfs;
  int selline[CTLDEPTH]; Arr *sel[CTLDEPTH]; int nsel;
  int jumped;
} CSt;

static void ctl_free(CSt *cs)
{
  while (cs->nfs > 0) a_rel(cs->fs[--cs->nfs].vec);
  while (cs->nsel > 0) a_rel(cs->sel[--cs->nsel]);
}

static int ctl_truth(Arr *v)
{
  int r;
  if (v->type != T_NUM || v->n != 1) {
    a_rel(v);
    rt_error("DOMAIN ERROR: a condition must be a numeric scalar");
  }
  r = v->num[0] != 0.0;
  a_rel(v);
  return r;
}

static Arr *ctl_expr(const char *s)
{
  Arr *v = eval_text(s);
  if (!v) syn_error("expression returned no value");
  return v;
}

static int ctl_step(Fn *f, int pc, CSt *cs)
{
  const char *rest = "";
  int k = f->ck[pc];
  (void)ctl_kind(f->lines[pc], &rest);
  switch (k) {
  case C_IF:
    if (ctl_truth(ctl_expr(rest))) { cs->jumped = 0; return pc + 1; }
    cs->jumped = 1; return f->cj[3 * pc];
  case C_ELSEIF:
    if (!cs->jumped) return f->cj[3 * pc + 1];
    if (ctl_truth(ctl_expr(rest))) { cs->jumped = 0; return pc + 1; }
    return f->cj[3 * pc];
  case C_ELSE:
    if (!cs->jumped) return f->cj[3 * pc + 1];
    cs->jumped = 0; return pc + 1;
  case C_ENDIF: cs->jumped = 0; return pc + 1;
  case C_WHILE:
    cs->jumped = 0;
    if (ctl_truth(ctl_expr(rest))) return pc + 1;
    return f->cj[3 * pc];
  case C_ENDWHILE: return f->cj[3 * pc];
  case C_REPEAT: return pc + 1;
  case C_UNTIL:
    if (ctl_truth(ctl_expr(rest))) return pc + 1;
    return f->cj[3 * pc];
  case C_FOR: {
    const char *p = rest;
    char nm[64];
    int n = 0;
    Arr *v, *it, *rv;
    FSt *fp;
    while (*p == ' ' || *p == '\t') p++;
    while ((is_alnum((unsigned char)*p) || *p == '_') && n < 63) nm[n++] = *p++;
    nm[n] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (n == 0 || p[0] != ':' || (p[1] != 'i' && p[1] != 'I') ||
        (p[2] != 'n' && p[2] != 'N'))
      syn_error(":For needs the form  :For name :In expression");
    assign_check(nm);
    p += 3;
    v = ctl_expr(p);
    rv = prim_mon(P_RAVEL, v);
    a_rel(v);
    if (rv->n == 0) { a_rel(rv); cs->jumped = 0; return f->cj[3 * pc]; }
    if (cs->nfs >= CTLDEPTH) { a_rel(rv); rt_error("DEPTH ERROR: :For loops nested too deeply"); }
    fp = &cs->fs[cs->nfs++];
    fp->line = pc; fp->vec = rv; fp->idx = 0;
    strcpy(fp->name, nm);
    it = a_item(rv, 0);
    var_set(nm, it);
    cs->jumped = 0;
    return pc + 1;
  }
  case C_ENDFOR: {
    FSt *fp;
    if (cs->nfs == 0) return pc + 1;
    fp = &cs->fs[cs->nfs - 1];
    fp->idx++;
    if (fp->idx < fp->vec->n) {
      var_set(fp->name, a_item(fp->vec, fp->idx));
      return fp->line + 1;
    }
    a_rel(fp->vec);
    cs->nfs--;
    return pc + 1;
  }
  case C_SELECT:
    if (cs->nsel >= CTLDEPTH) rt_error("DEPTH ERROR: :Select nested too deeply");
    cs->selline[cs->nsel] = pc;
    cs->sel[cs->nsel++] = ctl_expr(rest);
    cs->jumped = 1;
    return pc + 1;
  case C_CASE: case C_CASELIST: {
    Arr *v, *r;
    int ok;
    if (!cs->jumped) return f->cj[3 * pc + 1];
    v = ctl_expr(rest);
    if (k == C_CASE) r = prim_dya(P_MATCH, cs->sel[cs->nsel - 1], v);
    else r = prim_dya(P_MEMBER, cs->sel[cs->nsel - 1], v);
    a_rel(v);
    ok = r->type == T_NUM && r->n > 0 && r->num[0] != 0.0;
    a_rel(r);
    if (ok) { cs->jumped = 0; return pc + 1; }
    return f->cj[3 * pc];
  }
  case C_ENDSELECT:
    if (cs->nsel > 0) a_rel(cs->sel[--cs->nsel]);
    cs->jumped = 0;
    return pc + 1;
  case C_LEAVE: case C_CONTINUE: {
    int lp = f->cj[3 * pc + 2];
    if (k == C_LEAVE) {
      if (f->ck[lp] == C_FOR && cs->nfs > 0 && cs->fs[cs->nfs - 1].line == lp) {
        a_rel(cs->fs[--cs->nfs].vec);
      }
    }
    while (cs->nsel > 0 && cs->selline[cs->nsel - 1] > lp)
      a_rel(cs->sel[--cs->nsel]);
    cs->jumped = 0;
    return f->cj[3 * pc];
  }
  case C_RETURN:
    ctl_free(cs);
    return f->nlines + 1;
  }
  return pc + 1;
}

static Arr *call_fn(Fn *f, Arr *left, Arr *right)
{
  Frame *fr;
  int i, pc;
  Arr *result = NULL;
  CSt cs;
  if (f->lam) return call_lambda(f, left, right);
  if (fdepth >= MAXDEPTH) rt_error("DEPTH ERROR: calls nested too deeply");
  fr = &frames[fdepth++];
  fr->fn = f; fr->nsaved = 0; fr->sv = NULL;
  if (f->left) frame_save(fr, f->left);
  if (f->right) frame_save(fr, f->right);
  if (f->res) frame_save(fr, f->res);
  for (i = 0; i < f->nloc; i++) frame_save(fr, f->loc[i]);
  for (i = 1; i <= f->nlines; i++)
    if (f->label[i]) frame_save(fr, f->label[i]);
  if (f->left) var_set(f->left, a_ref(left));
  if (f->right) var_set(f->right, a_ref(right));
  for (i = 1; i <= f->nlines; i++)
    if (f->label[i]) var_set(f->label[i], a_scalar((double)i));
  pc = 1;
  cs.nfs = 0; cs.nsel = 0; cs.jumped = 0;
  if (!f->cbuilt) ctl_build(f);
  while (pc >= 1 && pc <= f->nlines) {
    Tok *t;
    if (f->hasctl && f->ck[pc]) { pc = ctl_step(f, pc, &cs); continue; }
    if (!f->tk[pc]) f->tk[pc] = lex(f->lines[pc]);
    t = f->tk[pc];
    if (f->label[pc]) t += 2;
    exec_tokens(t);
    if (br_set) {
      br_set = 0;
      pc = br_target;
    } else {
      pc++;
    }
  }
  ctl_free(&cs);
  if (f->res) {
    Arr *v = var_get(f->res);
    if (!v) rt_error_s("function result not set: ", f->res, "");
    result = a_ref(v);
  }
  unwind_frames(fdepth - 1);
  return result;
}
/* ===== p5_sys.c ===== */
/* p5_sys.c - function editor, system commands, workspaces, REPL, main */

/* ---------- small string helpers ---------- */
static char *trim_dup(const char *s)
{
  const char *e;
  while (is_space((unsigned char)*s)) s++;
  e = s + strlen(s);
  while (e > s && is_space((unsigned char)e[-1])) e--;
  return xstrndup(s, (int)(e - s));
}
static int ieq(const char *a, const char *b)
{
  while (*a && *b) {
    if (to_upper((unsigned char)*a) != to_upper((unsigned char)*b)) return 0;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}
/* copy the next whitespace-delimited word of *s into w; advance *s */
static int next_word(const char **s, char *w, int max)
{
  int n = 0;
  while (is_space((unsigned char)**s)) (*s)++;
  if (**s == '\0') return 0;
  while (**s && !is_space((unsigned char)**s)) {
    if (n < max - 1) w[n++] = **s;
    (*s)++;
  }
  w[n] = '\0';
  return 1;
}
static int read_line(FILE *fp, char *buf, int max)
{
  size_t n;
  if (!fgets(buf, max, fp)) return 0;
  n = strlen(buf);
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
  return 1;
}

/* ---------- header parsing ---------- */
typedef struct HInfo {
  char name[64];
  int arity;
  char res[64], left[64], right[64];
  int nloc;
  char loc[16][64];
} HInfo;

static void fmt_header(const HInfo *h, char *buf)
{
  int i;
  buf[0] = '\0';
  if (h->res[0]) { strcat(buf, h->res); strcat(buf, "<-"); }
  if (h->arity == 2) { strcat(buf, h->left); strcat(buf, " "); }
  strcat(buf, h->name);
  if (h->arity >= 1) { strcat(buf, " "); strcat(buf, h->right); }
  for (i = 0; i < h->nloc; i++) { strcat(buf, ";"); strcat(buf, h->loc[i]); }
}

static int name_is_function(const char *nm)
{
  return fn_find(nm) != NULL || reserved_word(nm);
}
static int valid_ident(const char *s)
{
  if (!is_alpha((unsigned char)*s)) return 0;
  while (*s) { if (!is_alnum((unsigned char)*s)) return 0; s++; }
  return 1;
}

/* returns 1 on success; prints a message and returns 0 otherwise */
static int header_parse(const char *text, HInfo *h)
{
  char *t = trim_dup(text);
  char *semi, *arrow;
  const char *rhs;
  char w[3][64];
  int nw = 0, i;
  memset(h, 0, sizeof(*h));
  semi = strchr(t, ';');
  if (semi) {
    const char *r = semi + 1;
    char tmp[256];
    *semi = '\0';
    while (*r) {
      int n = 0;
      char *tt;
      while (*r && *r != ';') { if (n < 255) tmp[n++] = *r; r++; }
      tmp[n] = '\0';
      tt = trim_dup(tmp);
      if (tt[0]) {
        if (!valid_ident(tt) || h->nloc >= 16) {
          printf("apl: bad function header: %s\n", text);
          free(tt); free(t);
          return 0;
        }
        strcpy(h->loc[h->nloc++], tt);
      }
      free(tt);
      if (*r == ';') r++;
    }
  }
  arrow = strstr(t, "<-");
  if (arrow) {
    char *res;
    *arrow = '\0';
    res = trim_dup(t);
    if (!valid_ident(res)) {
      printf("apl: bad function header (expected a single result-variable"
             " name before <-)\n");
      free(res); free(t);
      return 0;
    }
    strcpy(h->res, res);
    free(res);
    rhs = arrow + 2;
  } else {
    rhs = t;
  }
  while (nw < 3 && next_word(&rhs, w[nw], 64)) nw++;
  { char extra[64]; if (next_word(&rhs, extra, 64)) nw = 9; }
  if (nw < 1 || nw > 3) {
    printf("apl: bad function header: %s\n", text);
    free(t);
    return 0;
  }
  for (i = 0; i < nw; i++)
    if (!valid_ident(w[i])) {
      printf("apl: bad function header: %s\n", text);
      free(t);
      return 0;
    }
  if (nw == 1) { h->arity = 0; strcpy(h->name, w[0]); }
  else if (nw == 2) {
    h->arity = 1; strcpy(h->name, w[0]); strcpy(h->right, w[1]);
  } else {
    h->arity = 2; strcpy(h->left, w[0]); strcpy(h->name, w[1]);
    strcpy(h->right, w[2]);
  }
  free(t);
  if (ieq(h->name, "nabla") || ieq(h->name, "edit")) {
    printf("apl: %s is reserved and cannot be a function name\n", h->name);
    return 0;
  }
  if (name_is_function(h->name) && !fn_find(h->name)) {
    printf("apl: %s is a built-in primitive and cannot be redefined\n",
           h->name);
    return 0;
  }
  if (var_get(h->name)) {
    printf("apl: %s is already a variable; clear it first, or pick another"
           " name\n", h->name);
    return 0;
  }
  {
    const char *names[24];
    int nn = 0;
    if (h->left[0]) names[nn++] = h->left;
    if (h->right[0]) names[nn++] = h->right;
    if (h->res[0]) names[nn++] = h->res;
    for (i = 0; i < h->nloc; i++) names[nn++] = h->loc[i];
    for (i = 0; i < nn; i++) {
      int j;
      if (strcmp(names[i], h->name) == 0) {
        printf("apl: %s is both this function's own name and one of its "
               "parameters/result/locals -- pick a different name for one of"
               " them\n", names[i]);
        return 0;
      }
      if (name_is_function(names[i])) {
        printf("apl: %s is already a function or primitive name -- pick a "
               "different parameter/result/local name\n", names[i]);
        return 0;
      }
      for (j = 0; j < i; j++)
        if (strcmp(names[i], names[j]) == 0) {
          printf("apl: %s appears twice in the function header\n", names[i]);
          return 0;
        }
    }
  }
  return 1;
}

/* ---------- editor ---------- */
typedef struct ELine { double key; char *text; } ELine;

static double key_round(double k) { return floor(k * 1000.0 + 0.5) / 1000.0; }

static void fmt_key(double k, char *buf)
{
  char *p;
  sprintf(buf, "%.3f", k);
  p = buf + strlen(buf) - 1;
  while (p > buf && *p == '0') *p-- = '\0';
  if (*p == '.') *p = '\0';
}
static double key_increment(const char *txt)
{
  const char *dot = strchr(txt, '.');
  double u = 1.0;
  int n;
  if (!dot) return 1.0;
  n = (int)strlen(dot + 1);
  while (n-- > 0) u /= 10.0;
  return u;
}
static int is_number_text(const char *s)
{
  int digits = 0, dot = 0;
  if (*s == '\0') return 0;
  for (; *s; s++) {
    if (is_digit((unsigned char)*s)) digits++;
    else if (*s == '.' && !dot) dot = 1;
    else return 0;
  }
  return digits > 0;
}

static void hinfo_from_fn(const Fn *f, HInfo *h)
{
  int i;
  memset(h, 0, sizeof(*h));
  strcpy(h->name, f->name);
  h->arity = f->arity;
  if (f->res) strcpy(h->res, f->res);
  if (f->left) strcpy(h->left, f->left);
  if (f->right) strcpy(h->right, f->right);
  h->nloc = f->nloc;
  for (i = 0; i < f->nloc; i++) strcpy(h->loc[i], f->loc[i]);
}

static void fn_show(const Fn *f)
{
  int i;
  if (f->lam) { printf("     %s <- %s\n", f->name, f->hdr); return; }
  printf("     %s\n", f->hdr);
  for (i = 1; i <= f->nlines; i++) printf("[%d]  %s\n", i, f->lines[i]);
}

static char *label_of(const char *line)
{
  const char *p = line;
  while (is_space((unsigned char)*p)) p++;
  if (is_alpha((unsigned char)*p)) {
    const char *q = p;
    while (is_alnum((unsigned char)*q)) q++;
    if (*q == ':' && q > p) return xstrndup(p, (int)(q - p));
  }
  return NULL;
}

static void fn_commit(const HInfo *h, ELine *el, int nel)
{
  Fn *f = (Fn *)xmalloc(sizeof(Fn));
  char buf[512];
  int i;
  f->name = xstrdup(h->name);
  f->arity = h->arity;
  f->res = h->res[0] ? xstrdup(h->res) : NULL;
  f->left = h->left[0] ? xstrdup(h->left) : NULL;
  f->right = h->right[0] ? xstrdup(h->right) : NULL;
  fmt_header(h, buf);
  f->hdr = xstrdup(buf);
  f->nloc = h->nloc;
  f->loc = (char **)xmalloc(sizeof(char *) * (size_t)(h->nloc + 1));
  for (i = 0; i < h->nloc; i++) f->loc[i] = xstrdup(h->loc[i]);
  f->lam = 0; f->ambi = 0; f->nstmt = 0; f->stk = NULL;
  f->cbuilt = 0; f->hasctl = 0; f->ck = NULL; f->cj = NULL;
  f->opn = 0; f->optok = NULL;
  f->nlines = nel;
  f->lines = (char **)xmalloc(sizeof(char *) * (size_t)(nel + 1));
  f->tk = (Tok **)xmalloc(sizeof(Tok *) * (size_t)(nel + 1));
  f->label = (char **)xmalloc(sizeof(char *) * (size_t)(nel + 1));
  f->lines[0] = NULL; f->tk[0] = NULL; f->label[0] = NULL;
  for (i = 1; i <= nel; i++) {
    f->lines[i] = el[i - 1].text;     /* takes ownership */
    f->tk[i] = NULL;
    f->label[i] = label_of(f->lines[i]);
  }
  fn_install(f);
}

/* the editor. rest = text after "nabla"; fp supplies further lines */
static void enter_editor(const char *rest, FILE *fp)
{
  char *ht = trim_dup(rest);
  HInfo h;
  ELine *el = NULL;
  int nel = 0, cap = 0, i;
  double nextkey = 1.0, lastinc = 1.0;
  char line[MAXLINE], kb[32], hb[512];
  Fn *existing = NULL;

  if (ht[0] == '\0') {
    printf("usage: nabla <header>   e.g. nabla Z<-FOO X\n"
           "   (or: nabla FOONAME to edit an existing function,\n"
           "    or: nabla FOONAME nabla  to just display it)\n");
    free(ht);
    return;
  }
  {
    const char *s = ht;
    char w1[64], w2[64], w3[64];
    int n = 0;
    n += next_word(&s, w1, 64);
    n += next_word(&s, w2, 64);
    n += next_word(&s, w3, 64);
    if (n == 2 && (ieq(w2, "nabla") || ieq(w2, "edit")) && fn_find(w1)) {
      fn_show(fn_find(w1));
      free(ht);
      return;
    }
    if (n == 1 && fn_find(w1)) existing = fn_find(w1);
    if (existing && existing->lam) {
      printf("%s is a lambda; redefine it with: %s <- { ... }\n", w1, w1);
      free(ht);
      return;
    }
  }
  if (existing) {
    hinfo_from_fn(existing, &h);
    fmt_header(&h, hb);
    printf("     %s\n", hb);
    cap = existing->nlines + 8;
    el = (ELine *)xmalloc(sizeof(ELine) * (size_t)cap);
    for (i = 1; i <= existing->nlines; i++) {
      el[nel].key = (double)i;
      el[nel].text = xstrdup(existing->lines[i]);
      nel++;
      printf("[%d]  %s\n", i, existing->lines[i]);
    }
    nextkey = (double)(existing->nlines + 1);
  } else {
    if (!header_parse(ht, &h)) { free(ht); return; }
    fmt_header(&h, hb);
    printf("     %s\n", hb);
    cap = 16;
    el = (ELine *)xmalloc(sizeof(ELine) * (size_t)cap);
  }
  free(ht);

  while (1) {
    char *ls;
    int isdir = 0;
    if (interactive) {
      fmt_key(nextkey, kb);
      printf("[%s]  ", kb);
      fflush(stdout);
    }
    if (!read_line(fp, line, (int)sizeof(line))) break;
    ls = trim_dup(line);
    if (ieq(ls, "nabla") || ieq(ls, "edit")) { free(ls); break; }
    if (strcmp(ls, "[]") == 0) {
      printf("     %s\n", hb);
      for (i = 0; i < nel; i++) {
        fmt_key(el[i].key, kb);
        printf("[%s]  %s\n", kb, el[i].text);
      }
      free(ls);
      continue;
    }
    if (ls[0] == '[') {
      char *cp = strchr(ls, ']');
      if (cp && cp - ls > 1) {
        char cand[64];
        int cl = (int)(cp - ls) - 1;
        if (cl < 63) {
          memcpy(cand, ls + 1, (size_t)cl);
          cand[cl] = '\0';
          {
            const char *tk = cand;
            if (*tk == '-') tk++;
            if (is_number_text(tk)) isdir = 1;
          }
        }
      }
      if (isdir) {
        char cand[64];
        char *body;
        int cl = (int)(cp - ls) - 1, del = 0, found = -1;
        double key;
        const char *ks;
        memcpy(cand, ls + 1, (size_t)cl);
        cand[cl] = '\0';
        ks = cand;
        if (*ks == '-') { del = 1; ks++; }
        key = key_round(strtod(ks, NULL));
        body = trim_dup(cp + 1);
        if (key <= 0.0) {
          printf("apl: bad line number: %s\n", ks);
          free(body); free(ls);
          continue;
        }
        for (i = 0; i < nel; i++)
          if (key_round(el[i].key) == key) { found = i; break; }
        if (del) {
          if (found < 0) printf("apl: no such line: %s\n", ks);
          else {
            free(el[found].text);
            for (i = found; i + 1 < nel; i++) el[i] = el[i + 1];
            nel--;
          }
          free(body); free(ls);
          continue;
        }
        if (body[0] == '\0') {
          if (found < 0) printf("apl: no such line: %s\n", ks);
          else {
            fmt_key(el[found].key, kb);
            printf("[%s]  %s\n", kb, el[found].text);
          }
          nextkey = key;
          lastinc = key_increment(ks);
          free(body); free(ls);
          continue;
        }
        if (found >= 0) {
          free(el[found].text);
          el[found].text = body;
        } else {
          int pos = nel;
          if (nel + 1 > cap) {
            cap *= 2;
            el = (ELine *)xrealloc(el, sizeof(ELine) * (size_t)cap);
          }
          for (i = 0; i < nel; i++)
            if (el[i].key > key) { pos = i; break; }
          for (i = nel; i > pos; i--) el[i] = el[i - 1];
          el[pos].key = key;
          el[pos].text = body;
          nel++;
        }
        nextkey = key;
        lastinc = key_increment(ks);
        free(ls);
        continue;
      }
    }
    free(ls);
    if (nel + 1 > cap) {
      cap *= 2;
      el = (ELine *)xrealloc(el, sizeof(ELine) * (size_t)cap);
    }
    el[nel].key = nextkey;
    el[nel].text = xstrdup(line);
    nel++;
    nextkey = key_round(nextkey + lastinc);
  }
  fn_commit(&h, el, nel);
  free(el);
  printf("     %s defined.\n", h.name);
}

/* ---------- quad functions: []NC []EX []CR []FX ---------- */
static int quad_is_fn(const char *name)
{
  return strcmp(name, "NC") == 0 || strcmp(name, "EX") == 0 ||
         strcmp(name, "CR") == 0 || strcmp(name, "FX") == 0 ||
         strcmp(name, "SIGNAL") == 0 || strcmp(name, "DL") == 0 ||
         strcmp(name, "VFI") == 0 || strcmp(name, "SVO") == 0 ||
         strcmp(name, "SVR") == 0 || strcmp(name, "SVQ") == 0;
}

/* row i of a text vector/matrix (blank-trimmed), as a new C string */
static char *text_row(const Arr *x, int i, int nrows)
{
  int w = (nrows == 1 && x->rank <= 1) ? x->n : x->shape[1], n;
  char *s = xstrndup(x->chr + i * w, w);
  n = (int)strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
  return s;
}

static int text_rows(const Arr *x)
{
  if (x->type != T_CHR) rt_error("DOMAIN ERROR: text expected");
  if (x->rank <= 1) return 1;
  if (x->rank == 2) return x->shape[0];
  rt_error("RANK ERROR: text vector or matrix expected");
  return 0;
}

static Arr *fx_define(Arr *x)
{
  ELine *el = NULL;
  HInfo h;
  int nrows, i, nel;
  Arr *r;
  char **rows;
  if (x->type == T_NEST) {
    nrows = x->n;
    rows = (char **)xmalloc(sizeof(char *) * (size_t)(nrows ? nrows : 1));
    for (i = 0; i < nrows; i++) {
      Arr *it = x->sub[i];
      if (it->type != T_CHR || it->rank > 1)
        { while (--i >= 0) free(rows[i]); free(rows); rt_error("DOMAIN ERROR: []FX needs text rows"); }
      rows[i] = xstrndup(it->chr, it->n);
    }
  } else {
    nrows = text_rows(x);
    rows = (char **)xmalloc(sizeof(char *) * (size_t)nrows);
    for (i = 0; i < nrows; i++) rows[i] = text_row(x, i, nrows);
  }
  if (nrows < 1) { free(rows); rt_error("DOMAIN ERROR: []FX needs a header row"); }
  if (!header_parse(rows[0], &h)) {
    for (i = 0; i < nrows; i++) free(rows[i]);
    free(rows);
    rt_error("[]FX: bad function header");
  }
  nel = nrows - 1;
  el = (ELine *)xmalloc(sizeof(ELine) * (size_t)(nel ? nel : 1));
  for (i = 0; i < nel; i++) { el[i].key = (double)(i + 1); el[i].text = rows[i + 1]; }
  free(rows[0]);
  free(rows);
  fn_commit(&h, el, nel);
  free(el);
  r = a_str(h.name);
  return r;
}

/* names (text vector or matrix) -> result per name */
static Arr *quad_svo(Arr *proc, Arr *names)
{
  int nrows, i, id;
  Arr *r;
  need_num(proc, P_INDEX);
  if (proc->n != 1) rt_error("RANK ERROR: []SVO needs one processor number on the left");
  id = as_int(proc->num[0], P_INDEX);
  nrows = text_rows(names);
  r = a_vec(T_NUM, nrows);
  for (i = 0; i < nrows; i++) {
    char *nm = text_row(names, i, nrows);
    Var *v;
    if (fn_find(nm)) { free(nm); a_rel(r); rt_error("DOMAIN ERROR: []SVO: a function cannot be shared"); }
    if (reserved_word(nm)) { free(nm); a_rel(r); rt_error("DOMAIN ERROR: []SVO: reserved word"); }
    trk_add(nm, 0);
    ap_do_offer(id, nm);
    trk_del(nm);
    v = var_find(nm, 1);
    v->ap = id;
    r->num[i] = 1.0;
    free(nm);
  }
  if (nrows == 1) { Arr *s = a_scalar(1.0); a_rel(r); return s; }
  return r;
}

static Arr *quad_fn(const char *name, Arr *x)
{
  int nrows, i;
  if (strcmp(name, "SVO") == 0 || strcmp(name, "SVR") == 0) {
    Arr *r;
    int retract = (name[2] == 'R');
    nrows = text_rows(x);
    r = a_vec(T_NUM, nrows);
    for (i = 0; i < nrows; i++) {
      char *nm = text_row(x, i, nrows);
      Var *v = var_find(nm, 0);
      int shared = (v && v->ap) ? 1 : 0;
      if (retract && shared) {
        int id = v->ap;
        v->ap = 0;
        ap_do_retract(id, nm);
      }
      r->num[i] = (double)shared;
      free(nm);
    }
    if (nrows == 1) { Arr *s = a_scalar(r->num[0]); a_rel(r); return s; }
    return r;
  }
  if (strcmp(name, "SVQ") == 0) {
    /* names currently shared with processor n */
    int id, k = 0, j;
    Var *v;
    Arr *r;
    need_num(x, P_INDEX);
    if (x->n != 1) rt_error("RANK ERROR: []SVQ needs one processor number");
    id = as_int(x->num[0], P_INDEX);
    for (j = 0; j < HSIZE; j++) for (v = vtab[j]; v; v = v->next) if (v->ap == id) k++;
    r = a_new(T_NEST, 1, &k);
    k = 0;
    for (j = 0; j < HSIZE; j++)
      for (v = vtab[j]; v; v = v->next)
        if (v->ap == id) a_put(r, k++, a_str(v->name));
    return a_simplify(r);
  }
  if (strcmp(name, "FX") == 0) return fx_define(x);
  if (strcmp(name, "SIGNAL") == 0) {
    if (x->type == T_CHR) {
      char *m = text_of(x, "[]SIGNAL");
      char buf[200];
      strncpy(buf, m, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      free(m);
      rt_error(buf);
    }
    if (x->type == T_NUM && x->n == 1) {
      int c = as_int(x->num[0], P_INDEX);
      switch (c) {
      case 1: rt_error("WS FULL"); break;
      case 2: syn_error("SYNTAX ERROR"); break;
      case 4: rt_error("RANK ERROR"); break;
      case 5: rt_error("LENGTH ERROR"); break;
      case 6: rt_error("VALUE ERROR"); break;
      case 7: rt_error("INDEX ERROR"); break;
      case 10: rt_error("AXIS ERROR"); break;
      case 11: rt_error("DOMAIN ERROR"); break;
      default: rt_error("DOMAIN ERROR: unknown []SIGNAL code");
      }
    }
    rt_error("DOMAIN ERROR: []SIGNAL needs an error number or text");
  }
  if (strcmp(name, "DL") == 0) {
    clock_t t0 = clock();
    double secs, el;
    need_num(x, P_INDEX);
    if (x->n != 1) rt_error("RANK ERROR: []DL takes one number");
    secs = x->num[0];
    if (secs < 0.0 || secs > 3600.0) rt_error("DOMAIN ERROR: []DL needs 0..3600 seconds");
    while ((double)(clock() - t0) / (double)CLOCKS_PER_SEC < secs) ;
    el = (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    return a_scalar(el);
  }
  if (strcmp(name, "VFI") == 0) {
    char *t = text_of(x, "[]VFI"), *q;
    int cnt = 0, k = 0, two = 2;
    Arr *ok, *val, *res;
    char *tok;
    q = t;
    trk_add(t, 0);
    while (*q) {
      while (*q == ' ') q++;
      if (!*q) break;
      cnt++;
      while (*q && *q != ' ') q++;
    }
    ok = a_vec(T_NUM, cnt);
    val = a_vec(T_NUM, cnt);
    q = t;
    while (k < cnt) {
      char *end;
      double v;
      while (*q == ' ') q++;
      tok = q;
      while (*q && *q != ' ') q++;
      {
        char save = *q;
        *q = '\0';
        v = strtod(tok, &end);
        if (end != tok && *end == '\0') { ok->num[k] = 1.0; val->num[k] = v; }
        else { ok->num[k] = 0.0; val->num[k] = 0.0; }
        *q = save;
      }
      k++;
    }
    trk_del(t);
    free(t);
    res = a_new(T_NEST, 1, &two);
    a_put(res, 0, ok);
    a_put(res, 1, val);
    return res;
  }
  if (strcmp(name, "CR") == 0) {
    char *nm;
    Fn *f;
    int w, sh[2];
    Arr *r;
    nrows = text_rows(x);
    if (nrows != 1) rt_error("RANK ERROR: []CR takes one name");
    nm = text_row(x, 0, 1);
    f = fn_find(nm);
    free(nm);
    if (!f) return a_vec(T_CHR, 0);
    if (f->lam) {
      char *line = (char *)xmalloc(strlen(f->name) + strlen(f->hdr) + 6);
      sprintf(line, "%s <- %s", f->name, f->hdr);
      r = a_str(line);
      free(line);
      return r;
    }
    w = (int)strlen(f->hdr);
    for (i = 1; i <= f->nlines; i++)
      if ((int)strlen(f->lines[i]) > w) w = (int)strlen(f->lines[i]);
    sh[0] = f->nlines + 1; sh[1] = w;
    r = a_new(T_CHR, 2, sh);
    memset(r->chr, ' ', (size_t)(sh[0] * w));
    memcpy(r->chr, f->hdr, strlen(f->hdr));
    for (i = 1; i <= f->nlines; i++)
      memcpy(r->chr + i * w, f->lines[i], strlen(f->lines[i]));
    return r;
  }
  /* NC and EX take one name per row */
  {
    Arr *r;
    nrows = text_rows(x);
    r = a_vec(T_NUM, nrows);
    for (i = 0; i < nrows; i++) {
      char *nm = text_row(x, i, nrows);
      int cls = 0;
      if (strcmp(name, "NC") == 0) {
        if (fn_find(nm)) cls = 3;
        else if (var_get(nm)) cls = 2;
        else cls = 0;
      } else {
        if (fn_find(nm)) cls = fn_remove(nm);
        else if (var_get(nm)) { var_set(nm, NULL); cls = 1; }
      }
      r->num[i] = (double)cls;
      free(nm);
    }
    if (nrows == 1) {
      Arr *s = a_scalar(r->num[0]);
      a_rel(r);
      return s;
    }
    return r;
  }
}

/* ---------- statements at top level ---------- */
static void mark_roots(void)
{
  int i;
  Var *v;
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (v->val) mark_arr(v->val);
}

/* run one statement; returns 0 on success, 1 on error (message printed) */
static int run_stmt(const char *ln)
{
  catch_top = 0;
  sstop = 0;
  if (setjmp(catch_jb[0]) == 0) {
    Tok *t = lex_t(ln);
    exec_tokens(t);
    lex_free(t);
    return 0;
  }
  catch_top = 0;
  trk_unwind(0);
  unwind_frames(0);
  br_set = 0;
  printf("apl: %s error -- %s\n", errkind, errmsg);
  fflush(stdout);
  mark_roots();
  sweep_unmarked();
  return 1;
}

/* ---------- platform: directory listing and file names ---------- */
#if defined(APL_HAVE_DIRENT)
#include <dirent.h>
#endif

static int cmp_str(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static char *ws_file_name(const char *name)
{
  size_t n = strlen(name);
  char *r = (char *)xmalloc(n + 8);
  strcpy(r, name);
  if (!(n >= 4 && strcmp(name + n - 4, ".apl") == 0)) strcat(r, ".apl");
  return r;
}
static char *ws_base_name(const char *name)
{
  size_t n = strlen(name);
  if (n >= 4 && strcmp(name + n - 4, ".apl") == 0) return xstrndup(name, (int)n - 4);
  return xstrdup(name);
}

static void sys_lib(void)
{
#if defined(APL_HAVE_DIRENT)
  DIR *d = opendir(".");
  struct dirent *e;
  char **names = NULL;
  int n = 0, i;
  if (d) {
    while ((e = readdir(d)) != NULL) {
      size_t l = strlen(e->d_name);
      if (l > 4 && strcmp(e->d_name + l - 4, ".apl") == 0) {
        names = (char **)xrealloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = ws_base_name(e->d_name);
      }
    }
    closedir(d);
  }
  if (n == 0) {
    printf("(no saved workspaces in the current directory)\n");
    free(names);
    return;
  }
  qsort(names, (size_t)n, sizeof(char *), cmp_str);
  printf("workspaces available in this directory:\n");
  for (i = 0; i < n; i++) {
    if (strcmp(names[i], wsid) == 0) printf("  %s   (active)\n", names[i]);
    else printf("  %s\n", names[i]);
    free(names[i]);
  }
  free(names);
#else
  printf("(workspace listing is not available on this platform)\n");
#endif
}

/* ---------- workspace save ---------- */
/* write an expression that rebuilds a nested array (strand form) */
static void emit_expr(FILE *fp, const Arr *a);
static void emit_simple(FILE *fp, const Arr *a)
{
  int i;
  char buf[64];
  if (a->type == T_CHR) {
    if (a->rank == 1 && a->n == 1) fprintf(fp, "(1) rho ");
    else if (a->rank != 1 && a->rank != 0) {
      fprintf(fp, "(");
      for (i = 0; i < a->rank; i++)
        fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
      fprintf(fp, ") rho ");
    }
    fprintf(fp, "'");
    for (i = 0; i < a->n; i++) {
      if (a->chr[i] == '\'') fputc('\'', fp);
      fputc(a->chr[i], fp);
    }
    fprintf(fp, "'");
    return;
  }
  if (a->rank == 0) { sprintf(buf, "%.15g", a->num[0]); fprintf(fp, "%s", buf); return; }
  if (a->rank > 1 || a->n <= 1) {
    fprintf(fp, "(");
    for (i = 0; i < a->rank; i++)
      fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
    fprintf(fp, ") rho ");
    if (a->n == 0) { fprintf(fp, "0"); return; }
    if (a->n == 1) { sprintf(buf, "%.15g", a->num[0]); fprintf(fp, "%s", buf); return; }
  }
  fprintf(fp, "(");
  for (i = 0; i < a->n; i++) {
    sprintf(buf, "%.15g", a->num[i]);
    fprintf(fp, "%s%s", i ? " " : "", buf);
  }
  fprintf(fp, ")");
}

static void emit_expr(FILE *fp, const Arr *a)
{
  int i;
  if (a->type != T_NEST) { emit_simple(fp, a); return; }
  if (a->rank == 0) {
    fprintf(fp, "enclose (");
    emit_expr(fp, a->sub[0]);
    fprintf(fp, ")");
    return;
  }
  if (a->rank > 1) {
    fprintf(fp, "(");
    for (i = 0; i < a->rank; i++)
      fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
    fprintf(fp, ") rho ");
  }
  if (a->n == 1) {
    if (a->rank == 1) fprintf(fp, "(1) rho ");
    fprintf(fp, "enclose (");
    emit_expr(fp, a->sub[0]);
    fprintf(fp, ")");
    return;
  }
  for (i = 0; i < a->n; i++) {
    fprintf(fp, i ? " (" : "(");
    emit_expr(fp, a->sub[i]);
    fprintf(fp, ")");
  }
}

static void save_value(FILE *fp, const char *name, const Arr *a)
{
  int i;
  char buf[64];
  fprintf(fp, "%s <- ", name);
  if (a->type == T_NEST) {
    emit_expr(fp, a);
    fprintf(fp, "\n");
    return;
  }
  if (a->type == T_CHR) {
    if (a->rank > 1) {
      fprintf(fp, "(");
      for (i = 0; i < a->rank; i++) fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
      fprintf(fp, ") rho ");
    } else if (a->rank == 1 && a->n == 1) {
      fprintf(fp, "(1) rho ");
    }
    fprintf(fp, "'");
    for (i = 0; i < a->n; i++) {
      if (a->chr[i] == '\'') fputc('\'', fp);
      fputc(a->chr[i], fp);
    }
    fprintf(fp, "'\n");
    return;
  }
  if (a->rank == 0) {
    sprintf(buf, "%.15g", a->num[0]);
    fprintf(fp, "%s\n", buf);
    return;
  }
  if (a->rank > 1 || a->n <= 1) {
    fprintf(fp, "(");
    for (i = 0; i < a->rank; i++) fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
    fprintf(fp, ") rho ");
    if (a->n == 0) { fprintf(fp, "0\n"); return; }
    if (a->n == 1) { sprintf(buf, "%.15g", a->num[0]); fprintf(fp, "%s\n", buf); return; }
  }
  fprintf(fp, "(");
  for (i = 0; i < a->n; i++) {
    sprintf(buf, "%.15g", a->num[i]);
    fprintf(fp, "%s%s", i ? " " : "", buf);
  }
  fprintf(fp, ")\n");
}

static void sys_save(const char *arg)
{
  char *name = trim_dup(arg), *fn;
  int nv = 0, nf = 0, i, nn = 0;
  Var *v;
  char **names = NULL;
  FILE *fp;
  if (name[0] == '\0') { free(name); name = xstrdup(wsid); }
  if (strcmp(name, "CLEAR WS") == 0) {
    printf("usage: )SAVE name   (or set one first with )WSID name)\n");
    free(name);
    return;
  }
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (v->val) {
        names = (char **)xrealloc(names, sizeof(char *) * (size_t)(nn + 1));
        names[nn++] = v->name;
      }
  if (nn == 0 && nfns == 0) {
    printf("(nothing to save)\n");
    free(name); free(names);
    return;
  }
  qsort(names, (size_t)nn, sizeof(char *), cmp_str);
  fn = ws_file_name(name);
  fp = fopen(fn, "w");
  if (!fp) {
    printf("apl: cannot write %s\n", fn);
    free(name); free(fn); free(names);
    return;
  }
  fprintf(fp, "# saved by )SAVE\n");
  for (i = 0; i < nn; i++) { save_value(fp, names[i], var_get(names[i])); nv++; }
  for (i = 0; i < nfns; i++) {
    int j;
    if (fns[i]->lam) {
      fprintf(fp, "%s <- %s\n", fns[i]->name, fns[i]->hdr);
      nf++;
      continue;
    }
    fprintf(fp, "nabla %s\n", fns[i]->hdr);
    for (j = 1; j <= fns[i]->nlines; j++) fprintf(fp, "%s\n", fns[i]->lines[j]);
    fprintf(fp, "nabla\n");
    nf++;
  }
  fclose(fp);
  { char *b = ws_base_name(fn); strncpy(wsid, b, sizeof(wsid) - 1); free(b); }
  printf("saved %d variable(s) and %d function(s) to %s\n", nv, nf, fn);
  free(name); free(fn); free(names);
}

static int run_stream(FILE *fp, int mode);   /* mode: 0 script, 1 load */

static void sys_load(const char *arg)
{
  char *name = trim_dup(arg), *fn;
  FILE *fp;
  if (name[0] == '\0') { printf("usage: )LOAD name\n"); free(name); return; }
  fn = ws_file_name(name);
  fp = fopen(fn, "r");
  if (!fp) {
    fp = fopen(name, "r");
    if (fp) { free(fn); fn = xstrdup(name); }
  }
  if (!fp) {
    printf("apl: cannot open %s (tried %s.apl and %s)\n", name, name, name);
    free(name); free(fn);
    return;
  }
  run_stream(fp, 1);
  fclose(fp);
  { char *b = ws_base_name(fn); strncpy(wsid, b, sizeof(wsid) - 1); free(b); }
  printf("loaded %s\n", fn);
  free(name); free(fn);
}

static void sys_clear(void)
{
  int i;
  Var *v;
  while (nfns > 0) fn_remove(fns[0]->name);
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next) {
      if (v->ap) { int id = v->ap; v->ap = 0; ap_do_retract(id, v->name); }
      a_rel(v->val); v->val = NULL;
    }
  Q_IO = 1;
  Q_PP = 6;
  strcpy(wsid, "CLEAR WS");
  printf("workspace cleared.\n");
}

static void sys_vars(void)
{
  char **names = NULL;
  int n = 0, i;
  Var *v;
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (v->val) {
        names = (char **)xrealloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = v->name;
      }
  if (n == 0) { printf("(no variables defined)\n"); free(names); return; }
  qsort(names, (size_t)n, sizeof(char *), cmp_str);
  for (i = 0; i < n; i++) printf("%s%s", i ? " " : "", names[i]);
  printf("\n");
  free(names);
}

static void sys_fns(void)
{
  int i;
  if (nfns == 0) {
    printf("(no user-defined functions -- use nabla/edit to define one; "
           "see )PRIMS for built-ins)\n");
    return;
  }
  for (i = 0; i < nfns; i++) {
    if (fns[i]->lam) printf("%s <- %s\n", fns[i]->name, fns[i]->hdr);
    else printf("%s\n", fns[i]->hdr);
  }
}

static void sys_prims(void)
{
  int i;
  printf("primitive functions (monadic and/or dyadic):\n ");
  for (i = 0; PRIMS[i].name; i++) {
    const char *nm = PRIMS[i].name;
    if (is_operator(PRIMS[i].id)) continue;
    if (strcmp(nm, "plus") == 0 || strcmp(nm, "minus") == 0 ||
        strcmp(nm, "times") == 0 || strcmp(nm, "divide") == 0) continue;
    printf(" %s", nm);
  }
  printf("\nhigher-order operators (prefix form -- e.g. \"reduce plus v\"):\n"
         "   reduce scan each outer inner swap repeat until rank\n   over atop amend keyed   (and your own:  name <- dop { alphaalpha ... })\n"
         "   (reduce/scan/each take a function; \"n reduce f x\" is n-wise,\n"
         "    \"x each f y\" is dyadic, \"x inner f g y\" is the inner product,\n"
         "    \"rank f k x\" applies f to rank-k cells; any operand may be a\n"
         "    { alpha ... omega } lambda; axis: \"reduce plus [1] m\")\n");
  printf("word aliases usable as a reduce/scan/each/outer function name:\n"
         "   plus minus times divide\n");
  printf("quad system variables/functions:\n"
         "  []IO  (settable, 0 or 1)   []PP  (settable, decimal places)"
         "   []TS  (read-only)\n"
         "  []CT  (comparison tolerance)   []RL  (random seed)   []A  []D\n"
         "  []NC name   []EX name   []CR name   []FX rows   (monadic)\n"
         "  []EA  (dyadic: primary []EA alternate -- Execute Alternate, "
         "i.e. try/catch)\n");
  printf("  []AV []TC []WA []AI []DM []EN  (read-only)   zilch (empty vector)\n"
         "  []SIGNAL n|text   []DL secs   []VFI text   (monadic)\n"
         "  n []SVO names   []SVO names   []SVR names   []SVQ n   "
         "(shared variables; see )PROCS)\n"
         );
  printf("control words (inside a nabla function, one per line):\n"
         "  :If :ElseIf :Else :EndIf   :While :EndWhile   :Repeat :Until\n"
         "  :For v :In x :EndFor   :Select :Case :CaseList :Else :EndSelect\n"
         "  :Leave :Continue :Return\n");
  printf("branching (inside a nabla-defined function only):\n"
         "  -> n        branch to line n          -> label     branch to a "
         "label\n"
         "  ->          exit the function         -> cond compress label   "
         "conditional branch\n");
}

static void sys_help(void)
{
  printf("system commands:\n"
   "  )VARS         list defined variable names\n"
   "  )FNS          list user-defined function names (define one with nabla/edit)\n"
   "  )PRIMS        list built-in primitive functions/operators/quad-vars\n"
   "  )LIB          list saved workspaces in the current directory\n"
   "  )WSID [name]  show, or set, the active workspace id\n");
  printf(
   "  )CLEAR        wipe all variables and functions (fresh workspace)\n"
   "  )SAVE [name]  save the workspace (defaults to the active )WSID)\n"
   "  )LOAD name    load a saved workspace into the current session\n"
   "  )COPY name [obj ...]   bring all (or the named) objects in from a workspace\n"
   "  )ERASE name [name2 ...]   delete variable(s)/function(s) by name\n"
   "  )PROCS        list auxiliary processors and the variables shared with them\n"
   "  )HELP  or )?  show this message\n"
   "  )QUIT )EXIT )OFF   end the session\n");
  printf("\n"
   "nabla Z<-NAME X   (or \"edit ...\")  define a function; see the manual\n");
}

/* )COPY name [object ...] : bring objects in from a saved workspace file */
static int in_list(char **list, int n, const char *s)
{
  int i;
  for (i = 0; i < n; i++) if (strcmp(list[i], s) == 0) return i + 1;
  return 0;
}

static void sys_copy(const char *arg)
{
  char *name = trim_dup(arg), *fn;
  const char *rest = name;
  char w[64], *objs[64];
  int nobj = 0, i, found[64], nfound = 0;
  FILE *fp, *tmp;
  char line[MAXLINE];
  if (!next_word(&rest, w, 64)) {
    printf("usage: )COPY name [object ...]\n");
    free(name);
    return;
  }
  {
    char *file = xstrdup(w);
    char ob[64];
    while (nobj < 64 && next_word(&rest, ob, 64)) objs[nobj++] = xstrdup(ob);
    free(name);
    name = file;
  }
  for (i = 0; i < 64; i++) found[i] = 0;
  fn = ws_file_name(name);
  fp = fopen(fn, "r");
  if (!fp) {
    fp = fopen(name, "r");
    if (fp) { free(fn); fn = xstrdup(name); }
  }
  if (!fp) {
    printf("apl: cannot open %s (tried %s.apl and %s)\n", name, name, name);
    goto done;
  }
  if (nobj == 0) {
    run_stream(fp, 1);
    fclose(fp);
    printf("copied all of %s\n", fn);
    goto done;
  }
  tmp = tmpfile();
  if (!tmp) {
    fclose(fp);
    printf("apl: cannot create a temporary file for )COPY\n");
    goto done;
  }
  while (read_line(fp, line, (int)sizeof(line))) {
    char *t = trim_dup(line);
    const char *r = t;
    char first[64];
    next_word(&r, first, 64);
    if (ieq(first, "nabla") || ieq(first, "edit")) {
      HInfo h;
      int keep = 0;
      if (header_parse(r, &h)) {
        int k = in_list(objs, nobj, h.name);
        if (k) { keep = 1; found[k - 1] = 1; }
      }
      if (keep) fprintf(tmp, "%s\n", line);
      while (read_line(fp, line, (int)sizeof(line))) {
        char *u = trim_dup(line);
        int end = ieq(u, "nabla") || ieq(u, "edit");
        free(u);
        if (keep) fprintf(tmp, "%s\n", line);
        if (end) break;
      }
    } else {
      int n = 0;
      char id[64];
      while (is_alnum((unsigned char)t[n]) && n < 63) { id[n] = t[n]; n++; }
      id[n] = '\0';
      if (n > 0) {
        const char *after = t + n;
        int k;
        while (is_space((unsigned char)*after)) after++;
        k = in_list(objs, nobj, id);
        if (k && after[0] == '<' && after[1] == '-') {
          found[k - 1] = 1;
          fprintf(tmp, "%s\n", line);
        }
      }
    }
    free(t);
  }
  fclose(fp);
  rewind(tmp);
  run_stream(tmp, 1);
  fclose(tmp);
  for (i = 0; i < nobj; i++) if (found[i]) nfound++;
  printf("copied %d of %d object(s) from %s\n", nfound, nobj, fn);
  for (i = 0; i < nobj; i++)
    if (!found[i]) printf("  not found: %s\n", objs[i]);
done:
  for (i = 0; i < nobj; i++) free(objs[i]);
  free(name); free(fn);
}

static void sys_erase(const char *arg)
{
  const char *s = arg;
  char w[128];
  int any = 0;
  while (next_word(&s, w, 128)) {
    int erased = 0;
    any = 1;
    if (var_get(w)) { var_set(w, NULL); erased = 1; }
    if (fn_find(w)) { fn_remove(w); erased = 1; }
    if (erased) printf("erased: %s\n", w);
    else printf("apl: not found (no such variable or function): %s\n", w);
  }
  if (!any) printf("usage: )ERASE name [name2 ...]\n");
}

static void sys_procs(void)
{
  int i, j;
  Var *v;
  printf("  no.  processor   state\n");
  for (i = 0; i < ap_n; i++) {
    printf("  %3d  %-10s  %s\n", ap_tab[i].id, ap_tab[i].name,
           ap_tab[i].installed ? "installed" : "reserved, not installed");
    for (j = 0; j < HSIZE; j++)
      for (v = vtab[j]; v; v = v->next)
        if (v->ap == ap_tab[i].id) printf("         shares: %s\n", v->name);
  }
}

static void sys_wsid(const char *arg)
{
  char *a = trim_dup(arg);
  if (a[0] == '\0') printf("%s\n", wsid);
  else {
    char *b = ws_base_name(a);
    strncpy(wsid, b, sizeof(wsid) - 1);
    free(b);
    printf("workspace id set to: %s\n", wsid);
  }
  free(a);
}

/* returns 1 to quit */
static int handle_sys(const char *ln)
{
  char cmd[64];
  const char *rest = ln;
  if (!next_word(&rest, cmd, 64)) return 0;
  if (ieq(cmd, ")VARS")) sys_vars();
  else if (ieq(cmd, ")FNS")) sys_fns();
  else if (ieq(cmd, ")PRIMS")) sys_prims();
  else if (ieq(cmd, ")LIB")) sys_lib();
  else if (ieq(cmd, ")WSID")) sys_wsid(rest);
  else if (ieq(cmd, ")CLEAR")) sys_clear();
  else if (ieq(cmd, ")SAVE")) sys_save(rest);
  else if (ieq(cmd, ")LOAD")) sys_load(rest);
  else if (ieq(cmd, ")COPY")) sys_copy(rest);
  else if (ieq(cmd, ")ERASE")) sys_erase(rest);
  else if (ieq(cmd, ")PROCS")) sys_procs();
  else if (ieq(cmd, ")HELP") || strcmp(cmd, ")?") == 0) sys_help();
  else if (ieq(cmd, ")QUIT") || ieq(cmd, ")EXIT") || ieq(cmd, ")OFF")) return 1;
  else printf("apl: unknown system command: %s (try )HELP)\n", cmd);
  return 0;
}

/* ---------- driving loops ---------- */
/* mode 0: script (error aborts, exit 1)   mode 1: load (continue on error)
 * mode 2: REPL (prompts, continue on error) */
static int run_stream(FILE *fp, int mode)
{
  char line[MAXLINE];
  while (1) {
    char *t;
    char first[16];
    const char *rest;
    if (mode == 2) { fputs("apl> ", stdout); fflush(stdout); }
    if (!read_line(fp, line, (int)sizeof(line))) {
      if (mode == 2) printf("\ngoodbye.\n");
      return 0;
    }
    t = trim_dup(line);
    if (t[0] == '\0') { free(t); continue; }
    rest = t;
    next_word(&rest, first, 16);
    if (ieq(first, "nabla") || ieq(first, "edit")) {
      int save = interactive;
      interactive = (mode == 2);
      enter_editor(rest, fp);
      interactive = save;
      free(t);
      continue;
    }
    if (t[0] == ')') {
      if (mode == 1) { free(t); continue; }     /* ignored inside )LOAD */
      if (handle_sys(t)) {
        if (mode == 2) printf("goodbye.\n");
        free(t);
        return 0;
      }
      free(t);
      continue;
    }
    if (run_stmt(line) && mode == 0) { free(t); return 1; }
    free(t);
  }
}

int main(int argc, char **argv)
{
  int rc;
  ap_init();
  fflush(stdout);
  if (argc > 1) {
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
      printf("apl: cannot open source file: %s\n", argv[1]);
      return 1;
    }
    interactive = 0;
    rc = run_stream(fp, 0);
    fclose(fp);
    return rc;
  }
  interactive = 1;
  printf("APL-in-C  --  type )help for commands, )quit to exit\n");
  run_stream(stdin, 2);
  return 0;
}
