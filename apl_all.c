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

struct Scope;                  /* namespace / class / instance (p4b_oo.c) */

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
  struct Scope *obj;           /* set on an object-reference handle vector */
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
  a->obj = NULL;
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
static void obj_release(struct Scope *s);
static const char *obj_text(const struct Scope *s);
static void a_free(Arr *a)
{
  a_unlink(a);
  if (a->obj) obj_release(a->obj);
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
      if (a->obj) obj_release(a->obj);
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
  if (a->obj) return blk_text(obj_text(a->obj));
  if (a->type == T_NEST && a->rank == 0 && a->sub[0] && a->sub[0]->obj)
    return blk_text(obj_text(a->sub[0]->obj));
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
      /* dotted path:  ns.name   obj.member   obj.[]NL */
      while (*q == '.' && (is_alpha((unsigned char)q[1]) ||
             (q[1] == '[' && q[2] == ']' && is_alpha((unsigned char)q[3])))) {
        q++;
        if (*q == '[') q += 2;
        while (is_alnum((unsigned char)*q)) q++;
      }
      len = (int)(q - p);
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
        if (*q == '.' && is_alpha((unsigned char)q[1])) {
          /* []THIS.member, []ROOT.name ...: a dotted path (K_NAME) */
          while (*q == '.' && is_alpha((unsigned char)q[1])) {
            q++;
            while (is_alnum((unsigned char)*q)) q++;
          }
          k = K_NAME;
          s = xstrndup(p, (int)(q - p));
        } else
        s = xstrndup(b, (int)(q - b));
      } else {
        k = K_QUAD;
      }
      p = q;
    } else {
      const char *sym = NULL;
      if (p[0] == '.' && n > 0 && is_alpha((unsigned char)p[1]) &&
          t[n - 1].k == K_SYM &&
          (t[n - 1].s[0] == ')' || t[n - 1].s[0] == ']') && !t[n - 1].s[1]) sym = ".";
      else if (p[0] == '<' && p[1] == '-') sym = "<-";
      else if (p[0] == '-' && p[1] == '>') sym = "->";
      else if (p[0] == '<' && p[1] == '=') sym = "<=";
      else if (p[0] == '>' && p[1] == '=') sym = ">=";
      else if (p[0] == '<' && p[1] == '>') sym = "<>";
      if (sym) {
        k = K_SYM; s = xstrdup(sym); p += strlen(sym);
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
#define AP_MAXPROC 64

typedef struct ApProc {
  int id;
  const char *name;
  int installed;                       /* 0: reserved number, no code yet */
  void (*offer)(const char *var);
  Arr *(*get)(const char *var);
  Arr *(*set)(const char *var, Arr *val);
  void (*retract)(const char *var);
  int objid;                           /* >0: an object acts as the processor */
  Arr *oref;                           /* ... and this keeps it alive */
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
  p->objid = 0; p->oref = NULL;
}

/* processors that are objects (p4b_oo.c / p5b_oosys.c) */
static void obj_ap_offer(const ApProc *p, const char *var);
static Arr *obj_ap_get(const ApProc *p, const char *var);
static Arr *obj_ap_set(const ApProc *p, const char *var, Arr *val);
static void obj_ap_retract(const ApProc *p, const char *var);

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
  if (p->objid) { obj_ap_offer(p, var); return; }
  if (p->offer) p->offer(var);
}
static Arr *ap_do_get(int id, const char *var)
{
  ApProc *p = ap_find(id);
  if (p && p->objid) return obj_ap_get(p, var);
  return (p && p->get) ? p->get(var) : NULL;
}
static Arr *ap_do_set(int id, const char *var, Arr *val)
{
  ApProc *p = ap_find(id);
  if (p && p->objid) return obj_ap_set(p, var, val);
  return (p && p->set) ? p->set(var, val) : NULL;
}
static void ap_do_retract(int id, const char *var)
{
  ApProc *p = ap_find(id);
  if (p && p->objid) { obj_ap_retract(p, var); return; }
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
/* The workspace keeps one table of variables and one list of functions,
 * both keyed by an absolute key.  The root (classic APL) workspace uses the
 * plain name.  A namespace, class or instance (p4b_oo.c) owns a key prefix
 * ("Util.", "Account.", "\1I7.") so its members are ordinary table entries
 * with a qualified key.  cur_scope selects the scope that unqualified names
 * are looked up in (NULL = root = exactly the classic behaviour). */
typedef struct Var {
  char *name;              /* absolute key */
  Arr *val;
  int ap;                  /* processor this variable is shared with, or 0 */
  struct Var *next;
} Var;
#define HSIZE 211
static Var *vtab[HSIZE];

struct Scope;
struct Fn;
struct Member;
static struct Scope *cur_scope = NULL;  /* NULL: root workspace */
static struct Scope *cur_owner = NULL;  /* class of the running method */
static Arr *prop_tmp = NULL;            /* last property-getter result */

#define KEYMAX 300
#define R_NONE 0
#define R_VAR 1       /* a variable: key */
#define R_FN 2        /* a function / bound method: fn */
#define R_SCOPE 3     /* a namespace, class, interface or instance: sc */
#define R_PROP 4      /* a property of sc: mem, decl */
#define R_QUAD 5      /* obj.[]NAME: qname applies to sc */
typedef struct Res {
  int kind;
  char key[KEYMAX];
  struct Fn *fn;
  struct Scope *sc, *decl;
  struct Member *mem;
  char qname[32];
} Res;

static unsigned hashname(const char *s)
{
  unsigned h = 5381u;
  while (*s) h = h * 33u + (unsigned char)*s++;
  return h % HSIZE;
}
static void oo_note_var(const char *key);
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
  if (name[0] == '\1') oo_note_var(name);
  return v;
}
/* scope-aware resolution (p4b_oo.c).  mode: 0 read, 1 write/delete an
 * existing name, 2 write (create when absent).  Returns 0 if not found. */
static int oo_find(const char *name, int mode, Res *r);
static Arr *oo_prop_read(Res *r);
static void oo_prop_write(Res *r, Arr *a);
static char *oo_local_key(const char *name);
static char *oo_fn_key(const char *name);

static Arr *var_value(Var *v)
{
  if (v->ap) {
    Arr *g = ap_do_get(v->ap, v->name);
    if (g) { a_rel(v->val); v->val = g; }
  }
  return v->val;
}
static Arr *var_get(const char *name)
{
  Var *v;
  if (cur_scope || strchr(name, '.')) {
    Res r;
    if (!oo_find(name, 0, &r)) return NULL;
    if (r.kind == R_PROP) return oo_prop_read(&r);
    if (r.kind != R_VAR) return NULL;
    v = var_find(r.key, 0);
  } else v = var_find(name, 0);
  if (!v) return NULL;
  return var_value(v);
}
/* store through an exact Var; takes ownership of one reference to a */
static void var_store(Var *v, Arr *a)
{
  if (v->ap) {
    if (a) {
      Arr *rep = ap_do_set(v->ap, v->name, a);
      if (rep) { a_rel(a); a = rep; }
    } else {
      int id = v->ap;
      v->ap = 0;
      ap_do_retract(id, v->name);
    }
  }
  a_rel(v->val);
  v->val = a;
}
/* takes ownership of one reference to a (may be NULL: unbind) */
static void var_set(const char *name, Arr *a)
{
  Var *v;
  if (cur_scope || strchr(name, '.')) {
    Res r;
    if (!oo_find(name, a != NULL ? 2 : 1, &r)) {
      if (a) { a_rel(a); rt_error_s("VALUE ERROR: no such member: ", name, ""); }
      return;
    }
    if (r.kind == R_PROP) {
      if (!a) rt_error("DOMAIN ERROR: a property cannot be expunged");
      oo_prop_write(&r, a);
      return;
    }
    if (r.kind != R_VAR) {
      if (a) { a_rel(a); rt_error_s("DOMAIN ERROR: cannot assign to ", name, ""); }
      return;
    }
    v = var_find(r.key, a != NULL);
  } else v = var_find(name, a != NULL);
  if (!v) return;
  var_store(v, a);
}
/* exact-key variants (frames restore by absolute key) */
static void var_set_key(const char *key, Arr *a)
{
  Var *v = var_find(key, a != NULL);
  if (!v) return;
  var_store(v, a);
}
static Arr *var_take_key(const char *key)
{
  Var *v = var_find(key, 0);
  Arr *a;
  if (!v) return NULL;
  a = v->val;
  v->val = NULL;
  return a;
}

/* ---------- user functions ---------- */
typedef struct Fn {
  char *name;                /* short name */
  char *key;                 /* absolute key (== name in the root) */
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
  struct Scope *sc;          /* home: namespace, or class of a method */
  int mflag;                 /* method attributes (F_xxx in p4b_oo.c) */
  struct Fn *master;         /* bound copy of this method ... */
  struct Scope *bsc;         /* ... running in this instance/class scope */
} Fn;

/* ---------- localization frames ---------- */
typedef struct Saved { char *key; Arr *val; } Saved;
typedef struct Frame {
  Fn *fn; int nsaved; Saved *sv;
  struct Scope *ssc, *sow;   /* caller's scope and owner class */
} Frame;
static Frame frames[MAXDEPTH + 2];
static int fdepth = 0;

static void unwind_frames(int depth)
{
  while (fdepth > depth) {
    Frame *fr = &frames[--fdepth];
    int i;
    for (i = fr->nsaved - 1; i >= 0; i--) {
      var_set_key(fr->sv[i].key, fr->sv[i].val);
      free(fr->sv[i].key);
    }
    free(fr->sv);
    cur_scope = fr->ssc;
    cur_owner = fr->sow;
  }
}

static Fn **fns = NULL;
static int nfns = 0;

static Fn *fn_find_key(const char *key)
{
  int i;
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->key, key) == 0) return fns[i];
  return NULL;
}
static Fn *oo_fn(const char *name);
static Fn *fn_find(const char *name)
{
  if (name[0] == 's' && strcmp(name, "self") == 0) {
    if (fdepth > 0 && frames[fdepth - 1].fn->lam) return frames[fdepth - 1].fn;
    return NULL;
  }
  if (cur_scope || strchr(name, '.')) return oo_fn(name);
  return fn_find_key(name);
}
static void fn_free(Fn *f)
{
  int i;
  if (f->master) { free(f->name); free(f->key); free(f); return; }
  free(f->name); free(f->key); free(f->res); free(f->left); free(f->right);
  free(f->hdr);
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
  if (!f->key) f->key = xstrdup(f->name);
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->key, f->key) == 0) {
      fn_free(fns[i]);
      fns[i] = f;
      return;
    }
  fns = (Fn **)xrealloc(fns, sizeof(Fn *) * (size_t)(nfns + 1));
  fns[nfns++] = f;
}
static int fn_remove_key(const char *key)
{
  int i;
  for (i = 0; i < nfns; i++)
    if (strcmp(fns[i]->key, key) == 0) {
      fn_free(fns[i]);
      for (; i + 1 < nfns; i++) fns[i] = fns[i + 1];
      nfns--;
      return 1;
    }
  return 0;
}
static int fn_remove(const char *name)
{
  char *k;
  int r;
  if (!cur_scope && !strchr(name, '.')) return fn_remove_key(name);
  k = oo_fn_key(name);
  r = fn_remove_key(k);
  free(k);
  return r;
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
static void oo_enter(Frame *fr, Fn *bound);
static int oo_quad_dya(const char *name);
static Arr *quad_dya(const char *name, Arr *x, Arr *y);
static Arr *oo_quad_in(const char *path, Arr *x);
static Arr *oo_name_value(const char *name);
static Arr *oo_quad_value(const char *name);
static int oo_member_of(Arr *obj, const char *path, Res *r);
static void fn_install_here(Fn *f);
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
  {
    Arr *o = oo_quad_value(name);
    if (o) return o;
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

/* index of the ")" matching the "(" at i, or -1 */
static int paren_end(const Tok *t, int i)
{
  int d = 0;
  for (; t[i].k != K_END; i++) {
    if (tok_sym(&t[i], "(")) d++;
    else if (tok_sym(&t[i], ")") && --d == 0) return i;
  }
  return -1;
}

/* (expr).member   p->i is at the "." ; obj is consumed */
static Arr *member_access(Ps *p, Arr *obj, int allow_args, Fn **mfn)
{
  Res r;
  const char *name;
  Arr *v = NULL, *res;
  p->i++;
  name = p->t[p->i].s;
  p->i++;
  if (!oo_member_of(obj, name, &r)) {
    a_rel(obj);
    rt_error_s("VALUE ERROR: no such member: ", name, "");
  }
  if (r.kind == R_VAR) {
    Var *var = var_find(r.key, 0);
    v = var ? var_value(var) : NULL;
    if (!v) { a_rel(obj); rt_error_s("VALUE ERROR: member has no value: ", name, ""); }
    res = a_ref(v);
  } else if (r.kind == R_PROP) {
    res = a_ref(oo_prop_read(&r));
  } else if (r.kind == R_FN) {
    Fn *f = r.fn;
    if (allow_args && (f->arity >= 1 || f->ambi)) {
      *mfn = f;
      return obj;                         /* the caller applies it */
    }
    if (f->arity != 0) {
      a_rel(obj);
      syn_error_s("a method that takes an argument cannot be used here: ", name);
    }
    res = call_fn(f, NULL, NULL);
    if (!res) { a_rel(obj); syn_error_s("method returned no value: ", name); }
  } else {
    a_rel(obj);
    rt_error_s("DOMAIN ERROR: not a value: ", name, "");
    res = NULL;
  }
  a_rel(obj);
  return res;
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
    if (tok_sym(&p->t[p->i], ".") && p->t[p->i + 1].k == K_NAME) {
      Fn *dummy = NULL;
      a = member_access(p, a, 0, &dummy);
    }
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
    if (!v) {
      Arr *ob = oo_name_value(t->s);        /* a class / namespace name */
      if (ob) { p->i++; return ob; }
      syn_error_s("undefined variable: ", t->s);
    }
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

static Arr *parse_operand_from(Ps *p, Arr *first)
{
  int base = sstop, n;
  Arr *r;
  for (;;) {
    Arr *a;
    if (first) { a = first; first = NULL; }
    else a = parse_item(p);
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
  if (t->k == K_QNAME && oo_quad_dya(t->s)) { *ea = 3; f->ntok = 1; return 1; }
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
  const char *qdn;
  /* leading function => monadic application */
  if (t->k == K_SYM && (strcmp(t->s, "+") == 0 || strcmp(t->s, "-") == 0)) {
    int id = (t->s[0] == '+') ? P_ADD : P_SUB;
    p->i++;
    r = need_val(parse_expr(p));
    res = prim_mon(id, r);
    a_rel(r);
    return res;
  }
  if (t->k == K_NAME && strstr(t->s, ".[]") != NULL) {
    /* obj.[]NL x : a system function evaluated in the scope obj */
    const char *qn = t->s;
    p->i++;
    r = need_val(parse_expr(p));
    res = oo_quad_in(qn, r);
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
  {
    Arr *pre = NULL;
    if (tok_sym(&p->t[p->i], "(")) {
      int e = paren_end(p->t, p->i);
      if (e > 0 && tok_sym(&p->t[e + 1], ".") && p->t[e + 2].k == K_NAME) {
        Arr *obj;
        Fn *mf = NULL;
        p->i++;
        obj = need_val(parse_expr(p));
        if (!tok_sym(&p->t[p->i], ")")) {
          a_rel(obj);
          syn_error("missing closing parenthesis");
        }
        p->i++;
        obj = member_access(p, obj, 1, &mf);
        if (mf) {
          Arr *rarg = need_val(parse_expr(p));
          res = call_fn(mf, NULL, rarg);
          a_rel(rarg);
          a_rel(obj);
          return res;
        }
        pre = obj;
        while (tok_sym(&p->t[p->i], "[")) {
          Arr *ix[MAXRANK], *rr;
          int nix, ii;
          nix = parse_index_list(p, ix);
          rr = select_p(pre, ix, nix);
          for (ii = 0; ii < nix; ii++) if (ix[ii]) a_rel(ix[ii]);
          a_rel(pre);
          pre = rr;
        }
      }
    }
    a = parse_operand_from(p, pre);
  }
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
    qdn = (ea == 3) ? p->t[p->i].s : NULL;
    p->i += f.ntok;
    f.axis = ea ? -1 : parse_axis_opt(p);
    r = need_val(parse_expr(p));
    if (ea == 3) res = quad_dya(qdn, a, r);
    else if (ea == 2) res = quad_svo(a, r);
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
    fn_install_here(nf);
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
    fn_install_here(nf);
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
  char *key = oo_local_key(name);
  for (i = 0; i < fr->nsaved; i++)
    if (strcmp(fr->sv[i].key, key) == 0) { free(key); return; }
  fr->sv = (Saved *)xrealloc(fr->sv, sizeof(Saved) * (size_t)(fr->nsaved + 1));
  fr->sv[fr->nsaved].key = key;
  fr->sv[fr->nsaved].val = var_take_key(key);
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
  f->key = NULL; f->sc = NULL; f->mflag = 0; f->master = NULL; f->bsc = NULL;
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
  oo_enter(fr, f);
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
  Fn *bound = f;
  if (f->master) f = f->master;
  if (f->lam) return call_lambda(f, left, right);
  if (fdepth >= MAXDEPTH) rt_error("DEPTH ERROR: calls nested too deeply");
  fr = &frames[fdepth++];
  fr->fn = f; fr->nsaved = 0; fr->sv = NULL;
  oo_enter(fr, bound);
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
/* ===== p4b_oo.c ===== */
/* p4b_oo.c - namespaces, classes and instances (the object layer)
 *
 * Everything is built on the one variable table and the one function list
 * of the evaluator.  A Scope owns a key prefix; its members are ordinary
 * table entries whose key is prefix + name:
 *     namespace  Util        prefix  "Util."          Util.f   Util.x
 *     class      Account     prefix  "Account."       methods, shared fields
 *     instance   #7          prefix  "\1I7."          instance fields
 * Unqualified names are looked up in cur_scope (NULL = the classic root).
 * An object reference is an enclosed character vector (the "handle") whose
 * Arr carries a pointer to its Scope; Scopes are reference counted through
 * those handles, and an instance whose count drops to zero is destroyed
 * (destructors run) at the end of the statement.
 */

#define SK_ROOT 0
#define SK_NS 1
#define SK_CLASS 2
#define SK_INST 3
#define SK_IFACE 4

/* method attributes (Fn.mflag) */
#define F_PUBLIC 1
#define F_SHARED 2
#define F_CTOR 4
#define F_DTOR 8
#define F_OVER 16
#define F_PGET 32
#define F_PSET 64

/* member kinds */
#define M_FIELD 1
#define M_PROP 2
#define M_EVENT 3
#define M_METHOD 4      /* interfaces only: a required method */

typedef struct Member {
  char *name;
  int kind;
  int pub, shared, ro;
  char *init;             /* field: initial-value expression text */
} Member;

typedef struct Hdl { char *ev; char *path; int home; } Hdl;

typedef struct Scope {
  int kind, id, anon, alive, refc, dying, inq, dtored;
  char *name, *pfx, *df;
  struct Scope *parent;      /* namespace: enclosing namespace */
  struct Scope *cls;         /* instance: its class */
  struct Scope *base;        /* class: base class */
  Member *mem; int nmem;
  struct Scope **ifs; int nif;     /* class: implemented interfaces */
  Hdl *hd; int nhd;                /* instance: event handlers */
  Fn **bound; int nbound;          /* bound copies of methods */
  char **vk; int nvk;              /* instance: keys of its variables */
} Scope;

static Scope **scopes = NULL;
static int nscopes = 0, capscopes = 0;
static Scope **decls = NULL;       /* namespaces, classes, interfaces */
static int ndecls = 0;
static int oo_restoring = 0;       /* )LOAD: access rules are suspended */
static Scope **dq = NULL;
static int ndq = 0, capdq = 0, oo_flushing = 0;
static Fn dummy_fn;
/* saved-object ids <-> scopes while a workspace is being loaded */
static Arr **idm_ref = NULL;
static int *idm_id = NULL;
static int idm_n = 0;

/* class-definition state (set while a :Class body is being read) */
static Scope *def_cls = NULL;
static int def_flags = 0;
static char *def_prop = NULL;
static Fn *edit_target = NULL;

static void kcat(char *buf, const char *a, const char *b)
{
  size_t la = strlen(a), lb = strlen(b);
  if (la + lb >= KEYMAX) rt_error("LIMIT ERROR: name too long");
  memcpy(buf, a, la);
  memcpy(buf + la, b, lb + 1);
}

/* ---------- scopes ---------- */
static Scope *root_scope(void);
static Scope *scope_new(int kind, const char *name)
{
  Scope *s;
  char buf[64];
  if (nscopes == 0 && kind != SK_ROOT) root_scope();
  s = (Scope *)xmalloc(sizeof(Scope));
  memset(s, 0, sizeof(Scope));
  if (nscopes == capscopes) {
    capscopes = capscopes ? capscopes * 2 : 64;
    scopes = (Scope **)xrealloc(scopes, sizeof(Scope *) * (size_t)capscopes);
  }
  s->id = nscopes;
  scopes[nscopes++] = s;
  s->kind = kind;
  s->alive = 1;
  s->name = xstrdup(name);
  if (kind == SK_INST) {
    sprintf(buf, "\1I%d.", s->id);
    s->pfx = xstrdup(buf);
  } else if (name[0] == '\0') {
    s->anon = 1;
    sprintf(buf, "\1N%d.", s->id);
    s->pfx = xstrdup(buf);
  } else {
    s->pfx = (char *)xmalloc(strlen(name) + 2);
    sprintf(s->pfx, "%s.", name);
  }
  if (kind != SK_INST && !s->anon && s->id > 0) {
    decls = (Scope **)xrealloc(decls, sizeof(Scope *) * (size_t)(ndecls + 1));
    decls[ndecls++] = s;
  }
  return s;
}

static Scope *root_scope(void)
{
  if (nscopes == 0) {
    Scope *r = scope_new(SK_ROOT, "#");
    free(r->pfx);
    r->pfx = xstrdup("");
    r->kind = SK_NS;
  }
  return scopes[0];
}

/* a namespace / class / interface called key (full name), or NULL */
static Scope *scope_find_decl(const char *key)
{
  int i;
  for (i = 0; i < ndecls; i++) {
    Scope *s = decls[i];
    if (s->alive && strcmp(s->name, key) == 0) return s;
  }
  return NULL;
}

static Scope *scope_by_id(int id)
{
  if (id < 0 || id >= nscopes) return NULL;
  return scopes[id];
}

static void scope_free(Scope *s)
{
  int i;
  if (s->id > 0 && s->id < nscopes && scopes[s->id] == s) scopes[s->id] = NULL;
  for (i = 0; i < ndecls; i++)
    if (decls[i] == s) { decls[i] = decls[--ndecls]; break; }
  for (i = 0; i < s->nmem; i++) { free(s->mem[i].name); free(s->mem[i].init); }
  free(s->mem);
  for (i = 0; i < s->nhd; i++) { free(s->hd[i].ev); free(s->hd[i].path); }
  free(s->hd);
  for (i = 0; i < s->nbound; i++) fn_free(s->bound[i]);
  free(s->bound);
  for (i = 0; i < s->nvk; i++) free(s->vk[i]);
  free(s->vk);
  free(s->ifs);
  free(s->name); free(s->pfx); free(s->df);
  free(s);
}

/* ---------- object references ---------- */
static Arr *obj_ref(Scope *s)
{
  char buf[32];
  Arr *h, *r;
  sprintf(buf, "\1%d", s->id);
  h = a_str(buf);
  h->obj = s;
  s->refc++;
  r = a_new(T_NEST, 0, NULL);
  r->sub[0] = h;
  return r;
}
static Scope *obj_of(const Arr *a)
{
  if (a->obj) return a->obj;
  if (a->type == T_NEST && a->rank == 0 && a->sub[0] && a->sub[0]->obj)
    return a->sub[0]->obj;
  return NULL;
}
static const char *obj_text(const Scope *s)
{
  static char buf[4][160];
  static int k = 0;
  char *b = buf[k = (k + 1) & 3];
  if (s->kind == SK_INST && s->df) sprintf(b, "%.150s", s->df);
  else if (s->kind == SK_INST) sprintf(b, "#.[%.100s]", s->cls ? s->cls->name : "?");
  else if (s->kind == SK_CLASS) sprintf(b, "#.[Class %.100s]", s->name);
  else if (s->kind == SK_IFACE) sprintf(b, "#.[Interface %.100s]", s->name);
  else if (s->id == 0) strcpy(b, "#");
  else if (s->anon) strcpy(b, "#.[Namespace]");
  else sprintf(b, "#.%.150s", s->name);
  return b;
}

static void dq_push(Scope *s)
{
  if (s->inq) return;
  if (ndq == capdq) {
    capdq = capdq ? capdq * 2 : 16;
    dq = (Scope **)realloc(dq, sizeof(Scope *) * (size_t)capdq);
    if (!dq) { capdq = 0; ndq = 0; return; }
  }
  s->inq = 1;
  dq[ndq++] = s;
}
static void obj_release(Scope *s)
{
  if (--s->refc > 0) return;
  s->refc = 0;
  if (!s->alive) {
    /* an expunged class/namespace nobody refers to any more */
    if (!s->inq) scope_free(s);
    return;
  }
  if ((s->kind == SK_INST || s->anon) && !s->dying) dq_push(s);
}

/* a variable was created under an instance prefix: remember its key */
static void oo_note_var(const char *key)
{
  int id = 0;
  const char *p = key + 2;
  Scope *s;
  while (is_digit((unsigned char)*p)) { id = id * 10 + (*p - '0'); p++; }
  s = scope_by_id(id);
  if (!s || !s->alive) return;
  s->vk = (char **)xrealloc(s->vk, sizeof(char *) * (size_t)(s->nvk + 1));
  s->vk[s->nvk++] = xstrdup(key);
}

/* remove a variable entry altogether */
static void var_drop(const char *key)
{
  unsigned h = hashname(key);
  Var **pp = &vtab[h], *v;
  while ((v = *pp) != NULL) {
    if (strcmp(v->name, key) == 0) {
      *pp = v->next;
      if (v->ap) { int id = v->ap; v->ap = 0; ap_do_retract(id, v->name); }
      a_rel(v->val);
      free(v->name);
      free(v);
      return;
    }
    pp = &v->next;
  }
}

/* ---------- member lookup ---------- */
static Member *cls_member(Scope *c, const char *m)
{
  int i;
  for (i = 0; i < c->nmem; i++)
    if (strcmp(c->mem[i].name, m) == 0) return &c->mem[i];
  return NULL;
}
static Member *chain_member(Scope *c, const char *m, Scope **decl)
{
  for (; c; c = c->base) {
    Member *mm = cls_member(c, m);
    if (mm) { *decl = c; return mm; }
  }
  return NULL;
}
static Fn *chain_method(Scope *c, const char *m, Scope **decl)
{
  char key[KEYMAX];
  for (; c; c = c->base) {
    Fn *f;
    kcat(key, c->pfx, m);
    f = fn_find_key(key);
    if (f && f->sc == c) { *decl = c; return f; }
  }
  return NULL;
}
static int is_derived(Scope *c, Scope *b)
{
  for (; c; c = c->base) if (c == b) return 1;
  return 0;
}
static int can_see(Scope *decl, int pub)
{
  return pub || oo_restoring || cur_owner == decl;
}

/* a copy of method m that runs in scope X (instance or class) */
static Fn *bind_fn(Fn *m, Scope *X)
{
  Fn *c;
  int i;
  for (i = 0; i < X->nbound; i++)
    if (X->bound[i]->master == m && X->bound[i]->arity == m->arity)
      return X->bound[i];
  c = (Fn *)xmalloc(sizeof(Fn));
  *c = *m;
  c->master = m;
  c->bsc = X;
  c->name = xstrdup(m->name);
  c->key = xstrdup(m->key);
  X->bound = (Fn **)xrealloc(X->bound, sizeof(Fn *) * (size_t)(X->nbound + 1));
  X->bound[X->nbound++] = c;
  return c;
}

static void priv_error(const char *m, const char *what)
{
  char buf[200];
  sprintf(buf, "ACCESS ERROR: %.80s is %.20s", m, what);
  rt_error(buf);
}

/* member m of a namespace (or the root) */
static int ns_member(Scope *X, const char *m, int mode, Res *r)
{
  Var *v;
  Fn *f;
  Scope *sub;
  kcat(r->key, X->pfx, m);
  v = var_find(r->key, 0);
  if (v && (v->val || v->ap || mode)) { r->kind = R_VAR; return 1; }
  f = fn_find_key(r->key);
  if (f) { r->kind = R_FN; r->fn = f; r->sc = X; return 1; }
  sub = scope_find_decl(r->key);
  if (sub) { r->kind = R_SCOPE; r->sc = sub; return 1; }
  if (mode == 2) { r->kind = R_VAR; return 1; }
  return 0;
}

/* member m of an instance or class X; start = first class to search */
static int inst_member(Scope *X, Scope *start, const char *m, int mode, Res *r)
{
  Scope *decl = NULL, *c;
  Member *mm;
  Fn *fn;
  c = start ? start : (X->kind == SK_INST ? X->cls : X);
  mm = chain_member(c, m, &decl);
  if (mm && mm->kind == M_FIELD) {
    if (!can_see(decl, mm->pub)) priv_error(m, "private");
    if (mode && mm->ro && !oo_restoring && cur_owner != decl)
      priv_error(m, "read-only");
    if (mm->shared) kcat(r->key, decl->pfx, m);
    else {
      if (X->kind != SK_INST) rt_error_s("DOMAIN ERROR: ", m, " is an instance field");
      kcat(r->key, X->pfx, m);
    }
    r->kind = R_VAR; r->mem = mm; r->decl = decl;
    return 1;
  }
  if (mm && mm->kind == M_PROP) {
    if (!can_see(decl, mm->pub)) priv_error(m, "private");
    r->kind = R_PROP; r->sc = X; r->mem = mm; r->decl = decl;
    return 1;
  }
  fn = chain_method(c, m, &decl);
  if (fn) {
    int vis = (fn->mflag & F_PUBLIC) || oo_restoring || cur_owner == decl ||
              ((fn->mflag & (F_CTOR | F_DTOR)) && cur_owner &&
               is_derived(cur_owner, decl));
    if (!vis) priv_error(m, "private");
    if (X->kind == SK_CLASS && !(fn->mflag & F_SHARED) && !oo_restoring)
      rt_error_s("DOMAIN ERROR: ", m, " is an instance method");
    r->kind = R_FN; r->fn = bind_fn(fn, X); r->decl = decl; r->sc = X;
    return 1;
  }
  /* an extra variable created by assignment inside a method */
  kcat(r->key, X->pfx, m);
  {
    Var *v = var_find(r->key, 0);
    if (v && (v->val || mode)) { r->kind = R_VAR; return 1; }
  }
  return 0;
}

static Scope *oo_this(void)
{
  if (!cur_scope) return root_scope();
  return cur_scope;
}

/* an unqualified name, looked up from the running scope outwards */
static int oo_unq(const char *name, int mode, Res *r)
{
  Scope *c = cur_scope;
  if (c) {
    if (c->kind == SK_NS) {
      if (ns_member(c, name, mode, r)) return 1;
    } else {
      if (inst_member(c, NULL, name, mode, r)) return 1;
      if (mode == 2) {
        kcat(r->key, c->pfx, name);
        r->kind = R_VAR;
        return 1;
      }
    }
    if (mode) return 0;
  }
  return ns_member(root_scope(), name, mode, r);
}

static Arr *oo_prop_get_value(Res *r);

static int oo_walk(char **seg, int nseg, Scope *X, Scope *start, int mode,
                   Res *r)
{
  int i;
  for (i = 0; i < nseg; i++) {
    int last = (i == nseg - 1);
    const char *m = seg[i];
    Res t;
    int found;
    if (!X->alive) rt_error("VALUE ERROR: that object has been expunged");
    if (m[0] == '[' && m[1] == ']') {
      if (!last) rt_error("SYNTAX ERROR: system name inside a path");
      r->kind = R_QUAD; r->sc = X;
      strncpy(r->qname, m + 2, sizeof(r->qname) - 1);
      r->qname[sizeof(r->qname) - 1] = '\0';
      return 1;
    }
    t.kind = R_NONE; t.fn = NULL; t.sc = NULL; t.decl = NULL; t.mem = NULL;
    t.key[0] = '\0'; t.qname[0] = '\0';
    if (X->kind == SK_NS) found = ns_member(X, m, last ? mode : 0, &t);
    else if (X->kind == SK_INST || X->kind == SK_CLASS)
      found = inst_member(X, start, m, last ? mode : 0, &t);
    else found = 0;
    start = NULL;
    if (!found) return 0;
    if (last) { *r = t; return 1; }
    if (t.kind == R_SCOPE) X = t.sc;
    else if (t.kind == R_VAR) {
      Var *v = var_find(t.key, 0);
      Arr *val = v ? var_value(v) : NULL;
      Scope *o = val ? obj_of(val) : NULL;
      if (!o) rt_error_s("DOMAIN ERROR: ", m, " is not a namespace or object");
      X = o;
    } else if (t.kind == R_PROP) {
      Arr *val = oo_prop_get_value(&t);
      Scope *o = val ? obj_of(val) : NULL;
      if (!o) rt_error_s("DOMAIN ERROR: ", m, " is not a namespace or object");
      X = o;
    } else rt_error_s("DOMAIN ERROR: ", m, " is not a namespace or object");
  }
  return 0;
}

static int oo_find(const char *name, int mode, Res *r)
{
  char buf[2 * KEYMAX];
  char *seg[24];
  int n = 0;
  char *p;
  Scope *X, *start = NULL;
  r->kind = R_NONE; r->fn = NULL; r->sc = NULL; r->decl = NULL;
  r->mem = NULL; r->key[0] = '\0'; r->qname[0] = '\0';
  if (!strchr(name, '.')) return oo_unq(name, mode, r);
  if (strlen(name) >= sizeof(buf)) rt_error("LIMIT ERROR: name too long");
  strcpy(buf, name);
  seg[n++] = buf;
  for (p = buf; *p; p++)
    if (*p == '.') {
      if (n >= 24) rt_error("LIMIT ERROR: path too long");
      *p = '\0';
      seg[n++] = p + 1;
    }
  if (strcmp(seg[0], "[]ROOT") == 0) X = root_scope();
  else if (strcmp(seg[0], "[]THIS") == 0) X = oo_this();
  else if (strcmp(seg[0], "[]BASE") == 0) {
    X = oo_this();
    start = cur_owner ? cur_owner->base : NULL;
    if (!start) rt_error("DOMAIN ERROR: []BASE used where there is no base class");
  } else if (strcmp(seg[0], "[]PARENT") == 0) {
    X = (cur_scope && cur_scope->parent) ? cur_scope->parent : root_scope();
  } else {
    Res t;
    if (!oo_unq(seg[0], 0, &t)) return 0;
    if (t.kind == R_SCOPE) X = t.sc;
    else if (t.kind == R_VAR) {
      Var *v = var_find(t.key, 0);
      Arr *val = v ? var_value(v) : NULL;
      Scope *o = val ? obj_of(val) : NULL;
      if (!o) rt_error_s("DOMAIN ERROR: ", seg[0], " is not a namespace or object");
      X = o;
    } else return 0;
  }
  return oo_walk(seg + 1, n - 1, X, start, mode, r);
}

static Fn *oo_fn(const char *name)
{
  Res r;
  if (!oo_find(name, 0, &r)) return NULL;
  return r.kind == R_FN ? r.fn : NULL;
}

/* absolute key under which a function-local name lives */
static char *oo_local_key(const char *name)
{
  char key[KEYMAX];
  if (!cur_scope) return xstrdup(name);
  kcat(key, cur_scope->pfx, name);
  return xstrdup(key);
}

/* absolute key of the function a (possibly dotted) name denotes */
static char *oo_fn_key(const char *name)
{
  Res r;
  if (strchr(name, '.') && oo_find(name, 0, &r) && r.kind == R_FN) {
    Fn *m = r.fn->master ? r.fn->master : r.fn;
    return xstrdup(m->key);
  }
  return oo_local_key(name);
}

/* ---------- scope switching ---------- */
static void oo_enter(Frame *fr, Fn *bound)
{
  Fn *m = bound->master ? bound->master : bound;
  Scope *nsc, *own;
  fr->ssc = cur_scope;
  fr->sow = cur_owner;
  if (m->lam && !m->key) return;          /* anonymous lambda: inherit */
  nsc = bound->master ? bound->bsc : m->sc;
  own = (m->sc && m->sc->kind == SK_CLASS) ? m->sc : NULL;
  cur_scope = (nsc && nsc->id == 0) ? NULL : nsc;
  cur_owner = own;
}

static void scope_push(Scope *sc, Scope *own)
{
  Frame *fr;
  if (fdepth >= MAXDEPTH) rt_error("DEPTH ERROR: calls nested too deeply");
  fr = &frames[fdepth++];
  fr->fn = &dummy_fn; fr->nsaved = 0; fr->sv = NULL;
  fr->ssc = cur_scope; fr->sow = cur_owner;
  cur_scope = (sc && sc->id == 0) ? NULL : sc;
  cur_owner = own;
}
static void scope_pop(void)
{
  unwind_frames(fdepth - 1);
}

/* install a function defined by the editor / )LOAD / []FX in the right
 * place: root, current namespace, or the class being defined */
static void fn_install_here(Fn *f)
{
  char key[KEYMAX];
  if (edit_target) {
    Fn *t = edit_target->master ? edit_target->master : edit_target;
    edit_target = NULL;
    f->sc = t->sc;
    f->mflag = t->mflag;
    f->key = xstrdup(t->key);
    fn_install(f);
    return;
  }
  if (def_cls) {
    f->sc = def_cls;
    if (def_prop) {
      int get = (strcmp(f->name, "get") == 0 || strcmp(f->name, "GET") == 0 ||
                 strcmp(f->name, "Get") == 0);
      int set = (strcmp(f->name, "set") == 0 || strcmp(f->name, "SET") == 0 ||
                 strcmp(f->name, "Set") == 0);
      char tail[16];
      if (!get && !set) {
        printf("apl: inside :Property only functions named get and set "
               "are allowed\n");
        fn_free(f);
        return;
      }
      if (get && !(f->arity == 0 && f->res)) {
        printf("apl: a property get function takes no argument and "
               "returns a result:  R<-get\n");
        fn_free(f);
        return;
      }
      if (set && !(f->arity == 1 && !f->res)) {
        printf("apl: a property set function takes the new value and "
               "returns nothing:  set V\n");
        fn_free(f);
        return;
      }
      strcpy(tail, get ? ":get" : ":set");
      kcat(key, def_cls->pfx, def_prop);
      if (strlen(key) + 5 >= KEYMAX) { fn_free(f); return; }
      strcat(key, tail);
      f->mflag = get ? F_PGET : F_PSET;
    } else {
      f->mflag = def_flags;
      if ((def_flags & F_CTOR) && f->arity > 1) {
        printf("apl: a constructor takes no argument or one argument\n");
        fn_free(f);
        def_flags = 0;
        return;
      }
      if ((def_flags & F_DTOR) && f->arity != 0) {
        printf("apl: a destructor takes no argument\n");
        fn_free(f);
        def_flags = 0;
        return;
      }
      kcat(key, def_cls->pfx, f->name);
    }
    def_flags = 0;
    f->key = xstrdup(key);
    fn_install(f);
    return;
  }
  if (cur_scope && cur_scope->kind == SK_NS) {
    f->sc = cur_scope;
    kcat(key, cur_scope->pfx, f->name);
    f->key = xstrdup(key);
    fn_install(f);
    return;
  }
  if (cur_scope) {
    printf("apl: functions cannot be defined inside an instance or class "
           "scope\n");
    fn_free(f);
    return;
  }
  f->key = xstrdup(f->name);
  fn_install(f);
}

/* ---------- properties ---------- */
static Arr *oo_prop_get_value(Res *r)
{
  char key[KEYMAX];
  Fn *g;
  Arr *v;
  kcat(key, r->decl->pfx, r->mem->name);
  if (strlen(key) + 5 >= KEYMAX) rt_error("LIMIT ERROR: name too long");
  strcat(key, ":get");
  g = fn_find_key(key);
  if (!g) rt_error_s("DOMAIN ERROR: property ", r->mem->name, " is write-only");
  v = call_fn(bind_fn(g, r->sc), NULL, NULL);
  if (!v) rt_error("VALUE ERROR: property getter returned no value");
  a_rel(prop_tmp);
  prop_tmp = v;
  return prop_tmp;
}
static Arr *oo_prop_read(Res *r)
{
  return oo_prop_get_value(r);
}
static void oo_prop_write(Res *r, Arr *a)
{
  char key[KEYMAX];
  Fn *s;
  Arr *res;
  kcat(key, r->decl->pfx, r->mem->name);
  if (strlen(key) + 5 >= KEYMAX) { a_rel(a); rt_error("LIMIT ERROR: name too long"); }
  strcat(key, ":set");
  s = fn_find_key(key);
  if (!s) { a_rel(a); rt_error_s("READONLY ERROR: property ", r->mem->name, " has no set function"); }
  res = call_fn(bind_fn(s, r->sc), NULL, a);
  a_rel(a);
  if (res) a_rel(res);
}

/* ---------- instances ---------- */
static Fn *find_ctor(Scope *cls, int arity, int *any)
{
  Scope *c;
  int i;
  *any = 0;
  for (c = cls; c; c = c->base)
    for (i = 0; i < nfns; i++)
      if (fns[i]->sc == c && (fns[i]->mflag & F_CTOR)) {
        *any = 1;
        if (fns[i]->arity == arity) return fns[i];
      }
  return NULL;
}

static Arr *oo_new(Scope *cls, Arr *args)
{
  Scope *inst, *chain[40], *c;
  Arr *ref, *res;
  int nch = 0, i, j, any;
  Fn *ctor;
  if (!cls || cls->kind != SK_CLASS) rt_error("DOMAIN ERROR: []NEW needs a class");
  if (!cls->alive) rt_error("VALUE ERROR: that class has been expunged");
  inst = scope_new(SK_INST, cls->name);
  inst->cls = cls;
  ref = obj_ref(inst);
  for (c = cls; c && nch < 40; c = c->base) chain[nch++] = c;
  for (i = nch - 1; i >= 0; i--) {
    c = chain[i];
    for (j = 0; j < c->nmem; j++) {
      Member *m = &c->mem[j];
      if (m->kind == M_FIELD && !m->shared && m->init) {
        Arr *v;
        scope_push(inst, c);
        v = eval_text(m->init);
        if (!v) rt_error_s("SYNTAX ERROR: no value for field ", m->name, "");
        var_set(m->name, v);
        scope_pop();
      }
    }
  }
  ctor = find_ctor(cls, args ? 1 : 0, &any);
  if (!ctor && (args || any)) {
    if (args) rt_error_s("DOMAIN ERROR: class ", cls->name, " has no constructor taking an argument");
    rt_error_s("DOMAIN ERROR: class ", cls->name, " needs an argument to be created");
  }
  if (ctor) {
    res = call_fn(bind_fn(ctor, inst), NULL, args);
    if (res) a_rel(res);
  }
  return ref;
}

static void run_dtors(Scope *s)
{
  Scope *c;
  int i;
  if (s->dtored) return;
  s->dtored = 1;
  for (c = s->cls; c; c = c->base)
    for (i = 0; i < nfns; i++)
      if (fns[i]->sc == c && (fns[i]->mflag & F_DTOR)) {
        Arr *r = call_fn(bind_fn(fns[i], s), NULL, NULL);
        if (r) a_rel(r);
      }
}

static void scope_destroy(Scope *s)
{
  int i;
  for (i = 0; i < s->nvk; i++) var_drop(s->vk[i]);
  s->alive = 0;
  scope_free(s);
}

/* end-of-statement: destroy instances nobody refers to any more */
static void oo_flush(void)
{
  if (oo_flushing) return;
  oo_flushing = 1;
  while (ndq > 0) {
    Scope *s = dq[--ndq];
    s->inq = 0;
    if (!s->alive) { if (s->refc <= 0) scope_free(s); continue; }
    if (s->refc > 0) continue;
    s->dying = 1;
    if (s->kind == SK_INST) run_dtors(s);
    if (s->refc > 0) { s->dying = 0; continue; }    /* resurrected */
    scope_destroy(s);
  }
  oo_flushing = 0;
}

/* run every destructor (workspace clear / end of session) */
static void oo_destroy_all(void)
{
  int i;
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_INST && !s->dying) {
      s->dying = 1;
      run_dtors(s);
    }
  }
}

static int oo_count_instances(Scope *cls)
{
  int i, n = 0;
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_INST && is_derived(s->cls, cls)) n++;
  }
  return n;
}

/* ---------- events ---------- */
static void oo_on(Scope *obj, const char *ev, const char *path)
{
  Scope *c;
  int ok = 0;
  Hdl *h;
  for (c = obj->cls; c; c = c->base) {
    Member *m = cls_member(c, ev);
    if (m && m->kind == M_EVENT) { ok = 1; break; }
  }
  if (!ok) rt_error_s("DOMAIN ERROR: no such event: ", ev, "");
  obj->hd = (Hdl *)xrealloc(obj->hd, sizeof(Hdl) * (size_t)(obj->nhd + 1));
  h = &obj->hd[obj->nhd++];
  h->ev = xstrdup(ev);
  h->path = xstrdup(path);
  h->home = cur_scope ? cur_scope->id : 0;
}

static void oo_raise(Scope *obj, const char *ev, Arr *data)
{
  int i, ok = 0, n;
  Scope *c;
  for (c = obj->cls; c; c = c->base) {
    Member *m = cls_member(c, ev);
    if (m && m->kind == M_EVENT) { ok = 1; break; }
  }
  if (!ok) rt_error_s("DOMAIN ERROR: no such event: ", ev, "");
  n = obj->nhd;
  for (i = 0; i < n && i < obj->nhd; i++) {
    Hdl h = obj->hd[i];
    Scope *home;
    Fn *f;
    Arr *res, *arg;
    if (strcmp(h.ev, ev) != 0) continue;
    home = scope_by_id(h.home);
    if (!home || !home->alive) continue;
    scope_push(home, home->kind == SK_INST ? home->cls : NULL);
    f = fn_find(h.path);
    if (!f) { scope_pop(); rt_error_s("VALUE ERROR: event handler not found: ", h.path, ""); }
    arg = data ? a_ref(data) : a_vec(T_NUM, 0);
    res = call_fn(f, NULL, f->arity >= 1 ? arg : NULL);
    a_rel(arg);
    if (res) a_rel(res);
    scope_pop();
  }
}

/* name of key relative to the current scope, or NULL if it belongs elsewhere */
static const char *oo_rel(const char *key)
{
  const char *rest;
  if (cur_scope) {
    size_t pl = strlen(cur_scope->pfx);
    if (strncmp(key, cur_scope->pfx, pl) != 0) return NULL;
    rest = key + pl;
  } else {
    if (key[0] == '\1') return NULL;
    rest = key;
  }
  return strchr(rest, '.') ? NULL : rest;
}

/* ---------- names as values ---------- */
static Arr *oo_name_value(const char *name)
{
  Res r;
  if (!cur_scope && !strchr(name, '.')) {
    Scope *s = scope_find_decl(name);
    return s ? obj_ref(s) : NULL;
  }
  if (!oo_find(name, 0, &r)) return NULL;
  if (r.kind == R_SCOPE) return obj_ref(r.sc);
  return NULL;
}

static Arr *oo_quad_value(const char *name)
{
  if (strcmp(name, "THIS") == 0) return obj_ref(oo_this());
  if (strcmp(name, "ROOT") == 0) return obj_ref(root_scope());
  if (strcmp(name, "PARENT") == 0)
    return obj_ref((cur_scope && cur_scope->parent) ? cur_scope->parent
                                                    : root_scope());
  if (strcmp(name, "CS") == 0) return obj_ref(cur_scope ? cur_scope : root_scope());
  return NULL;
}

/* ---------- is-a ---------- */
static int class_isa(Scope *c, Scope *t)
{
  for (; c; c = c->base) {
    int i;
    if (c == t) return 1;
    for (i = 0; i < c->nif; i++) if (c->ifs[i] == t) return 1;
  }
  return 0;
}



/* ---------- objects as auxiliary processors ---------- */
/* An object that has public methods  Offer Get Set Retract  can stand in
 * for a processor:  Offer N / R<-Get N / R<-N Set V / Retract N  */
static Fn *ap_method(const ApProc *p, const char *m, Scope **sc)
{
  Scope *s = scope_by_id(p->objid), *d;
  Fn *f;
  if (!s || !s->alive)
    rt_error("DOMAIN ERROR: the object serving as processor no longer exists");
  *sc = s;
  f = chain_method(s->cls, m, &d);
  if (f && !(f->mflag & F_PUBLIC)) f = NULL;
  return f;
}
static void obj_ap_offer(const ApProc *p, const char *var)
{
  Scope *s;
  Fn *f = ap_method(p, "Offer", &s);
  Arr *n, *r;
  if (!f) return;
  n = a_str(var);
  r = call_fn(bind_fn(f, s), NULL, n);
  a_rel(n);
  if (r) a_rel(r);
}
static Arr *obj_ap_get(const ApProc *p, const char *var)
{
  Scope *s;
  Fn *f = ap_method(p, "Get", &s);
  Arr *n, *r;
  if (!f) return NULL;
  n = a_str(var);
  r = call_fn(bind_fn(f, s), NULL, n);
  a_rel(n);
  return r;
}
static Arr *obj_ap_set(const ApProc *p, const char *var, Arr *val)
{
  Scope *s;
  Fn *f = ap_method(p, "Set", &s);
  Arr *n, *r;
  if (!f) return NULL;
  n = a_str(var);
  r = call_fn(bind_fn(f, s), n, val);
  a_rel(n);
  return r;
}
static void obj_ap_retract(const ApProc *p, const char *var)
{
  Scope *s;
  Fn *f = ap_method(p, "Retract", &s);
  Arr *n, *r;
  if (!f) return;
  n = a_str(var);
  r = call_fn(bind_fn(f, s), NULL, n);
  a_rel(n);
  if (r) a_rel(r);
}

/* the processor number for object s (registering it when new) */
static int ap_for_object(Scope *s, int id)
{
  int i;
  if (!s || s->kind != SK_INST) rt_error("DOMAIN ERROR: a processor must be an instance of a class");
  for (i = 0; i < ap_n; i++)
    if (ap_tab[i].objid == s->id && (id == 0 || ap_tab[i].id == id))
      return ap_tab[i].id;
  if (id == 0) {
    id = 1000;
    while (ap_find(id)) id++;
  } else if (ap_find(id)) rt_error("DOMAIN ERROR: that processor number is already in use");
  if (ap_n >= AP_MAXPROC) rt_error("WS FULL: too many processors");
  ap_register(id, "OBJECT", 1, NULL, NULL, NULL, NULL);
  ap_tab[ap_n - 1].objid = s->id;
  ap_tab[ap_n - 1].oref = obj_ref(s);
  return id;
}

/* mark roots owned by this layer */
static void oo_mark_roots(void)
{
  int i;
  if (prop_tmp) mark_arr(prop_tmp);
  for (i = 0; i < ap_n; i++) if (ap_tab[i].oref) mark_arr(ap_tab[i].oref);
  for (i = 0; i < idm_n; i++) mark_arr(idm_ref[i]);
}

/* member `path` of the object value obj (for  (expr).member ) */
static int oo_member_of(Arr *obj, const char *path, Res *r)
{
  char buf[2 * KEYMAX];
  char *seg[24], *q;
  int n = 0;
  Scope *X = obj_of(obj);
  r->kind = R_NONE; r->fn = NULL; r->sc = NULL; r->decl = NULL;
  r->mem = NULL; r->key[0] = '\0'; r->qname[0] = '\0';
  if (!X) rt_error("DOMAIN ERROR: a member can only be selected from a namespace or object");
  if (strlen(path) >= sizeof(buf)) rt_error("LIMIT ERROR: name too long");
  strcpy(buf, path);
  seg[n++] = buf;
  for (q = buf; *q; q++)
    if (*q == '.') {
      if (n >= 24) rt_error("LIMIT ERROR: path too long");
      *q = '\0';
      seg[n++] = q + 1;
    }
  return oo_walk(seg, n, X, NULL, 0, r);
}
/* ===== p5_sys.c ===== */
/* p5_sys.c - function editor, system commands, workspaces, REPL, main */

/* object layer (p4b_oo.c, p5b_oosys.c) */
static int oo_quad_is_fn(const char *name);
static int oo_quad_fn(const char *name, Arr *x, Arr **out);
static int oo_is_block(const char *first);
static int oo_block(FILE *fp, const char *line, int mode);
static int guarded(void (*fn)(void *), void *arg);
static double oo_nc(const char *nm);
static int oo_ex(const char *nm);
static void oo_clear_all(void);
static void oo_clear_finish(void);
static void oo_flush(void);
static void oo_destroy_all(void);
static void oo_mark_roots(void);
static void emit_ref(FILE *fp, struct Scope *s);
static void emit_fn(FILE *fp, const Fn *f);
static void oo_save_defs(FILE *fp, int *ncls, int *nns);
static void oo_save_instances(FILE *fp, int *ninst);
static void oo_save_values(FILE *fp);
static void oo_save_procs(FILE *fp, int *nshare);
static void oo_load_begin(void);
static void oo_load_end(void);
static void sys_cs(const char *arg);
static void sys_classes(void);
static void sys_nss(void);
static void sys_ifaces(void);
static const char *oo_rel(const char *key);
static Fn *ed_lookup(const char *name);
static int run_stream_until(FILE *fp, int mode, const char *endword);
static void guarded_flush_(void);

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
  if (!def_cls && var_get(h->name)) {
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
  f->key = NULL; f->sc = NULL; f->mflag = 0; f->master = NULL; f->bsc = NULL;
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
  fn_install_here(f);
}

/* the function a nabla command names: inside a :Class body the methods of
 * that class, otherwise whatever the (possibly dotted) name denotes */
static Fn *ed_lookup(const char *name)
{
  Fn *f;
  if (def_cls) {
    char key[KEYMAX];
    kcat(key, def_cls->pfx, name);
    return fn_find_key(key);
  }
  oo_restoring++;
  f = fn_find(name);
  oo_restoring--;
  return f;
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
    if (n == 2 && (ieq(w2, "nabla") || ieq(w2, "edit")) && ed_lookup(w1)) {
      fn_show(ed_lookup(w1));
      free(ht);
      return;
    }
    if (n == 1 && ed_lookup(w1)) {
      existing = ed_lookup(w1);
      edit_target = existing;
    }
    if (existing && existing->lam) {
      printf("%s is a lambda; redefine it with: %s <- { ... }\n", w1, w1);
      edit_target = NULL;
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
         strcmp(name, "SVR") == 0 || strcmp(name, "SVQ") == 0 ||
         oo_quad_is_fn(name);
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
  if (obj_of(proc)) id = ap_for_object(obj_of(proc), 0);
  else {
    need_num(proc, P_INDEX);
    if (proc->n != 1) rt_error("RANK ERROR: []SVO needs one processor number on the left");
    id = as_int(proc->num[0], P_INDEX);
  }
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
  {
    Arr *o;
    if (oo_quad_fn(name, x, &o)) return o;
  }
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
    f = ed_lookup(nm);
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
      double cls = 0;
      trk_add(nm, 0);
      if (strcmp(name, "NC") == 0) {
        cls = oo_nc(nm);
      } else {
        if (fn_find(nm)) cls = (double)fn_remove(nm);
        else if (var_get(nm)) { var_set(nm, NULL); cls = 1; }
        else cls = (double)oo_ex(nm);
      }
      r->num[i] = cls;
      trk_del(nm);
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
  oo_mark_roots();
}

/* run one statement; returns 0 on success, 1 on error (message printed).
 * Statements may nest (a block body, []FIX), so each uses the next catch
 * slot and only the outermost one sweeps unreachable arrays. */
static int run_stmt(const char *ln)
{
  volatile int base = catch_top + 1;
  volatile int fd = fdepth, tm = ntrk;
  if (base >= MAXCATCH) { printf("apl: statements nested too deeply\n"); return 1; }
  catch_top = base;
  if (base == 0) sstop = 0;
  if (setjmp(catch_jb[base]) == 0) {
    Tok *t = lex_t(ln);
    exec_tokens(t);
    lex_free(t);
    oo_flush();
    catch_top = base - 1;
    return 0;
  }
  catch_top = base;
  trk_unwind(tm);
  unwind_frames(fd);
  br_set = 0;
  oo_flushing = 0;
  printf("apl: %s error -- %s\n", errkind, errmsg);
  fflush(stdout);
  if (base == 0 && fd == 0) {
    mark_roots();
    sweep_unmarked();
  }
  catch_top = base - 1;
  if (base == 0) {
    /* destroy what the failed statement left unreachable */
    guarded_flush_();
  }
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
  if (a->obj) { emit_ref(fp, a->obj); return; }
  if (a->type == T_NEST && a->rank == 0 && a->sub[0] && a->sub[0]->obj) {
    emit_ref(fp, a->sub[0]->obj);
    return;
  }
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
  int nv = 0, nf = 0, i, nn = 0, ncls = 0, nns = 0, ninst = 0, nshare = 0;
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
      if (v->val && !strchr(v->name, '.') && v->name[0] != '\1') {
        names = (char **)xrealloc(names, sizeof(char *) * (size_t)(nn + 1));
        names[nn++] = v->name;
      }
  if (nn == 0 && nfns == 0 && ndecls == 0 && nscopes <= 1) {
    printf("(nothing to save)\n");
    free(name); free(names);
    return;
  }
  if (nn > 1) qsort(names, (size_t)nn, sizeof(char *), cmp_str);
  fn = ws_file_name(name);
  fp = fopen(fn, "w");
  if (!fp) {
    printf("apl: cannot write %s\n", fn);
    free(name); free(fn); free(names);
    return;
  }
  fprintf(fp, "# saved by )SAVE\n");
  for (i = 0; i < nfns; i++)
    if (!fns[i]->sc) { emit_fn(fp, fns[i]); nf++; }
  oo_save_defs(fp, &ncls, &nns);
  oo_save_instances(fp, &ninst);
  for (i = 0; i < nn; i++) { save_value(fp, names[i], var_find(names[i], 0)->val); nv++; }
  oo_save_values(fp);
  oo_save_procs(fp, &nshare);
  fclose(fp);
  { char *b = ws_base_name(fn); strncpy(wsid, b, sizeof(wsid) - 1); free(b); }
  printf("saved %d variable(s) and %d function(s) to %s\n", nv, nf, fn);
  if (ncls || nns || ninst)
    printf("  also %d class(es), %d namespace(s), %d object(s)\n", ncls, nns, ninst);
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
  oo_load_begin();
  run_stream(fp, 1);
  oo_load_end();
  fclose(fp);
  { char *b = ws_base_name(fn); strncpy(wsid, b, sizeof(wsid) - 1); free(b); }
  printf("loaded %s\n", fn);
  free(name); free(fn);
}

static void sys_clear(void)
{
  int i;
  Var *v;
  oo_clear_all();
  while (nfns > 0) fn_remove_key(fns[0]->key);
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next) {
      if (v->ap) { int id = v->ap; v->ap = 0; ap_do_retract(id, v->name); }
      a_rel(v->val); v->val = NULL;
    }
  oo_clear_finish();
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
      if (v->val && oo_rel(v->name)) {
        names = (char **)xrealloc(names, sizeof(char *) * (size_t)(n + 1));
        names[n++] = (char *)oo_rel(v->name);
      }
  if (n == 0) { printf("(no variables defined)\n"); free(names); return; }
  qsort(names, (size_t)n, sizeof(char *), cmp_str);
  for (i = 0; i < n; i++) printf("%s%s", i ? " " : "", names[i]);
  printf("\n");
  free(names);
}

static void sys_fns(void)
{
  int i, any = 0;
  for (i = 0; i < nfns; i++) {
    if (!oo_rel(fns[i]->key)) continue;
    any = 1;
    if (fns[i]->lam) printf("%s <- %s\n", fns[i]->name, fns[i]->hdr);
    else printf("%s\n", fns[i]->hdr);
  }
  if (!any)
    printf("(no user-defined functions -- use nabla/edit to define one; "
           "see )PRIMS for built-ins)\n");
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
   "  )CS [ns]      change the current namespace (no argument: the root)\n");
  printf(
   "  )CLASSES  )NSS  )INTERFACES   list classes, namespaces, interfaces\n"
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
    oo_load_begin();
    run_stream(fp, 1);
    oo_load_end();
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
    if (oo_is_block(first) && !ieq(first, ":With")) {
      char bn[64], endw[32];
      int keep = 0;
      next_word(&r, bn, 64);
      strcpy(endw, ieq(first, ":Class") ? ":EndClass" :
             (ieq(first, ":Interface") ? ":EndInterface" : ":EndNamespace"));
      { int k = in_list(objs, nobj, bn); if (k) { keep = 1; found[k - 1] = 1; } }
      if (keep) fprintf(tmp, "%s\n", line);
      while (read_line(fp, line, (int)sizeof(line))) {
        char *u = trim_dup(line);
        const char *ur = u;
        char fw[32];
        int end = next_word(&ur, fw, 32) && ieq(fw, endw);
        free(u);
        if (keep) fprintf(tmp, "%s\n", line);
        if (end) break;
      }
    } else if (ieq(first, "nabla") || ieq(first, "edit")) {
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
  oo_load_begin();
  run_stream(tmp, 1);
  oo_load_end();
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
  else if (ieq(cmd, ")CS")) sys_cs(rest);
  else if (ieq(cmd, ")CLASSES")) sys_classes();
  else if (ieq(cmd, ")NSS")) sys_nss();
  else if (ieq(cmd, ")INTERFACES")) sys_ifaces();
  else if (ieq(cmd, ")HELP") || strcmp(cmd, ")?") == 0) sys_help();
  else if (ieq(cmd, ")QUIT") || ieq(cmd, ")EXIT") || ieq(cmd, ")OFF")) return 1;
  else printf("apl: unknown system command: %s (try )HELP)\n", cmd);
  return 0;
}

/* ---------- driving loops ---------- */
typedef struct SysCtx { const char *ln; int quit; } SysCtx;
static void sys_thunk(void *a)
{
  SysCtx *c = (SysCtx *)a;
  c->quit = handle_sys(c->ln);
}
typedef struct EdCtx { const char *rest; FILE *fp; } EdCtx;
static void ed_thunk(void *a)
{
  EdCtx *c = (EdCtx *)a;
  enter_editor(c->rest, c->fp);
}

/* mode 0: script (error aborts, exit 1)   mode 1: load (continue on error)
 * mode 2: REPL (prompts, continue on error).  With endword, the stream is a
 * block body and ends at that word (which is consumed). */
static int run_stream_until(FILE *fp, int mode, const char *endword)
{
  char line[MAXLINE];
  while (1) {
    char *t;
    char first[32];
    const char *rest;
    if (mode == 2 && !endword) { fputs("apl> ", stdout); fflush(stdout); }
    if (!read_line(fp, line, (int)sizeof(line))) {
      if (mode == 2 && !endword) printf("\ngoodbye.\n");
      return 0;
    }
    t = trim_dup(line);
    if (t[0] == '\0') { free(t); continue; }
    rest = t;
    next_word(&rest, first, 32);
    if (endword && ieq(first, endword)) { free(t); return 0; }
    if (ieq(first, "nabla") || ieq(first, "edit")) {
      int save = interactive;
      EdCtx c;
      int err;
      interactive = (mode == 2);
      c.rest = rest; c.fp = fp;
      err = guarded(ed_thunk, &c);
      interactive = save;
      free(t);
      if (err && mode == 0) return 1;
      continue;
    }
    if (first[0] == ':' && oo_is_block(first)) {
      int err = oo_block(fp, line, mode);
      free(t);
      if (err && mode == 0) return 1;
      continue;
    }
    if (t[0] == ')') {
      SysCtx c;
      if (mode == 1) { free(t); continue; }     /* ignored inside )LOAD */
      c.ln = t; c.quit = 0;
      guarded(sys_thunk, &c);
      if (c.quit) {
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
static int run_stream(FILE *fp, int mode)
{
  return run_stream_until(fp, mode, NULL);
}

static void destroy_thunk(void *a)
{
  (void)a;
  oo_destroy_all();
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
    guarded(destroy_thunk, NULL);
    return rc;
  }
  interactive = 1;
  printf("APL-in-C  --  type )help for commands, )quit to exit\n");
  run_stream(stdin, 2);
  guarded(destroy_thunk, NULL);
  return 0;
}
/* ===== p5b_oosys.c ===== */
/* p5b_oosys.c - the object layer's statements, system functions and
 * workspace support:  :Class :Interface :Namespace :With blocks, []NEW
 * []NS []CS []NL []NQ ..., )CS )CLASSES )NSS, and )SAVE / )LOAD of objects. */

static int run_stream_until(FILE *fp, int mode, const char *endword);
static void scope_expunge(Scope *s);

/* ---------- guarded execution of one block statement ---------- */
/* Statements that run outside run_stmt (class bodies and the like) need
 * their own catch frame, because a bare error would otherwise have nowhere
 * to land.  Returns 1 if an error was reported. */
static int guarded(void (*fn)(void *), void *arg)
{
  volatile int base = catch_top + 1;
  volatile int fd = fdepth, tm = ntrk;
  Scope *volatile ss = cur_scope;
  Scope *volatile so = cur_owner;
  if (base >= MAXCATCH) { printf("apl: blocks nested too deeply\n"); return 1; }
  catch_top = base;
  if (setjmp(catch_jb[base]) == 0) {
    fn(arg);
    catch_top = base - 1;
    return 0;
  }
  catch_top = base - 1;
  trk_unwind(tm);
  unwind_frames(fd);
  br_set = 0;
  cur_scope = ss;
  cur_owner = so;
  oo_flushing = 0;
  printf("apl: %s error -- %s\n", errkind, errmsg);
  fflush(stdout);
  return 1;
}

/* ---------- name helpers ---------- */
/* full name of a declaration made while the current scope is cur_scope */
static void qualify(const char *name, char *out)
{
  if (cur_scope && cur_scope->kind == SK_NS) kcat(out, cur_scope->pfx, name);
  else kcat(out, "", name);
}
/* find a class / interface / namespace by a name written in the current
 * scope: first relative to the current namespace, then absolute */
static Scope *decl_lookup(const char *name)
{
  char key[KEYMAX];
  Scope *s;
  if (cur_scope && cur_scope->kind == SK_NS) {
    kcat(key, cur_scope->pfx, name);
    s = scope_find_decl(key);
    if (s) return s;
  }
  return scope_find_decl(name);
}

/* make namespace `full` (and its parents) */
static Scope *ns_create(const char *full)
{
  char buf[KEYMAX];
  char *p, *start;
  Scope *par = root_scope(), *s = NULL;
  size_t n = strlen(full);
  if (n == 0 || n >= KEYMAX) rt_error("LIMIT ERROR: bad namespace name");
  strcpy(buf, full);
  start = buf;
  for (p = buf;; p++) {
    if (*p == '.' || *p == '\0') {
      int end = (*p == '\0');
      *p = '\0';
      if (start == p) rt_error("DOMAIN ERROR: empty name in namespace path");
      s = scope_find_decl(buf);
      if (!s) {
        if (var_find(buf, 0) && var_find(buf, 0)->val)
          rt_error_s("DOMAIN ERROR: name is a variable: ", buf, "");
        if (fn_find_key(buf))
          rt_error_s("DOMAIN ERROR: name is a function: ", buf, "");
        s = scope_new(SK_NS, buf);
        s->parent = (par->id == 0) ? NULL : par;
      } else if (s->kind != SK_NS)
        rt_error_s("DOMAIN ERROR: name is already a class or interface: ", buf, "");
      par = s;
      if (end) break;
      *p = '.';
      start = p + 1;
    }
  }
  return s;
}

static int valid_dotted(const char *s)
{
  char seg[64];
  while (*s) {
    int n = 0;
    while (*s && *s != '.') { if (n < 63) seg[n++] = *s; s++; }
    seg[n] = '\0';
    if (!valid_ident(seg)) return 0;
    if (*s == '.') { s++; if (!*s) return 0; }
  }
  return 1;
}

/* variables / functions / nested scopes under a prefix */
static void vars_drop_prefix(const char *pfx)
{
  char **keys = NULL;
  int n = 0, i;
  size_t pl = strlen(pfx);
  Var *v;
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (strncmp(v->name, pfx, pl) == 0) {
        keys = (char **)xrealloc(keys, sizeof(char *) * (size_t)(n + 1));
        keys[n++] = xstrdup(v->name);
      }
  for (i = 0; i < n; i++) { var_drop(keys[i]); free(keys[i]); }
  free(keys);
}
static void fns_drop_scope(Scope *sc)
{
  int i;
  for (i = nfns - 1; i >= 0; i--)
    if (fns[i]->sc == sc) fn_remove_key(fns[i]->key);
}

static void class_reset(Scope *cls)
{
  int i;
  fns_drop_scope(cls);
  vars_drop_prefix(cls->pfx);
  for (i = 0; i < cls->nmem; i++) { free(cls->mem[i].name); free(cls->mem[i].init); }
  free(cls->mem); cls->mem = NULL; cls->nmem = 0;
  free(cls->ifs); cls->ifs = NULL; cls->nif = 0;
  for (i = 0; i < cls->nbound; i++) fn_free(cls->bound[i]);
  free(cls->bound); cls->bound = NULL; cls->nbound = 0;
  cls->base = NULL;
}

static Member *add_member(Scope *cls, const char *name, int kind, int pub,
                          int shared, int ro, const char *init)
{
  Member *m;
  Scope *d;
  if (cls_member(cls, name))
    rt_error_s("DOMAIN ERROR: duplicate member: ", name, "");
  if (cls->base && chain_member(cls->base, name, &d))
    rt_error_s("DOMAIN ERROR: member already exists in a base class: ", name, "");
  cls->mem = (Member *)xrealloc(cls->mem, sizeof(Member) * (size_t)(cls->nmem + 1));
  m = &cls->mem[cls->nmem++];
  m->name = xstrdup(name);
  m->kind = kind; m->pub = pub; m->shared = shared; m->ro = ro;
  m->init = init ? xstrdup(init) : NULL;
  return m;
}

/* read and throw away lines up to (and including) a line starting endw */
static void skip_block(FILE *fp, const char *endw)
{
  char line[MAXLINE], w[32];
  while (read_line(fp, line, (int)sizeof(line))) {
    const char *r = line;
    if (next_word(&r, w, 32) && ieq(w, endw)) return;
  }
}

/* ---------- :Class ---------- */
typedef struct ClsCtx {
  FILE *fp;
  Scope *cls;
  char line[MAXLINE];
  int done;
} ClsCtx;

static int word_is_access(const char *w, int *pub, int *shared, int *ro)
{
  if (ieq(w, "Public")) { *pub = 1; return 1; }
  if (ieq(w, "Private")) { *pub = 0; return 1; }
  if (ieq(w, "Shared")) { *shared = 1; return 1; }
  if (ieq(w, "ReadOnly")) { *ro = 1; return 1; }
  return 0;
}

static void class_line(void *a)
{
  ClsCtx *k = (ClsCtx *)a;
  Scope *cls = k->cls;
  const char *rest = k->line;
  char w[64];
  if (!next_word(&rest, w, 64)) return;
  if (w[0] == '#' || ieq(w, "lamp")) return;
  if (ieq(w, ":EndClass")) { k->done = 1; return; }
  if (ieq(w, "nabla") || ieq(w, "edit")) {
    int save = interactive;
    interactive = 0;
    enter_editor(rest, k->fp);
    interactive = save;
    return;
  }
  if (ieq(w, ":Field")) {
    int pub = 0, shared = 0, ro = 0;
    char mw[64];
    char *spec, *arrow, *nn, *ini = NULL;
    for (;;) {
      const char *save = rest;
      if (!next_word(&rest, mw, 64)) rt_error("SYNTAX ERROR: :Field needs a name");
      if (!word_is_access(mw, &pub, &shared, &ro)) { rest = save; break; }
    }
    spec = trim_dup(rest);
    trk_add(spec, 0);
    arrow = strstr(spec, "<-");
    if (arrow) {
      *arrow = '\0';
      ini = trim_dup(arrow + 2);
      trk_add(ini, 0);
      if (ini[0] == '\0') rt_error("SYNTAX ERROR: :Field initial value missing");
    }
    nn = trim_dup(spec);
    trk_add(nn, 0);
    if (!valid_ident(nn))
      rt_error("SYNTAX ERROR: :Field  [Public|Private] [Shared] [ReadOnly] name [<- value]");
    add_member(cls, nn, M_FIELD, pub, shared, ro, ini);
    trk_del(nn); free(nn);
    if (ini) { trk_del(ini); free(ini); }
    trk_del(spec); free(spec);
    return;
  }
  if (ieq(w, ":Event")) {
    char nm[64];
    if (!next_word(&rest, nm, 64) || !valid_ident(nm))
      rt_error("SYNTAX ERROR: :Event needs a name");
    add_member(cls, nm, M_EVENT, 1, 0, 0, NULL);
    return;
  }
  if (ieq(w, ":Access") || ieq(w, ":Public") || ieq(w, ":Private") ||
      ieq(w, ":Shared") || ieq(w, ":Override") || ieq(w, ":Constructor") ||
      ieq(w, ":Destructor")) {
    int first = 1;
    for (;;) {
      char x[64];
      if (first) { strcpy(x, w); first = 0; }
      else if (!next_word(&rest, x, 64)) break;
      if (ieq(x, ":Access")) continue;
      if (ieq(x, ":Public") || ieq(x, "Public")) def_flags |= F_PUBLIC;
      else if (ieq(x, ":Private") || ieq(x, "Private")) def_flags &= ~F_PUBLIC;
      else if (ieq(x, ":Shared") || ieq(x, "Shared")) def_flags |= F_SHARED;
      else if (ieq(x, ":Override") || ieq(x, "Override")) def_flags |= F_OVER;
      else if (ieq(x, ":Constructor")) def_flags |= F_CTOR;
      else if (ieq(x, ":Destructor")) def_flags |= F_DTOR;
      else rt_error_s("SYNTAX ERROR: unknown modifier: ", x, "");
    }
    return;
  }
  if (ieq(w, ":Property")) {
    int pub = 0, shared = 0, ro = 0;
    char nm[64], line[MAXLINE], x[32];
    int ended = 0;
    for (;;) {
      const char *save = rest;
      if (!next_word(&rest, nm, 64)) rt_error("SYNTAX ERROR: :Property needs a name");
      if (!word_is_access(nm, &pub, &shared, &ro)) { rest = save; break; }
    }
    next_word(&rest, nm, 64);
    if (!valid_ident(nm)) rt_error("SYNTAX ERROR: bad property name");
    add_member(cls, nm, M_PROP, pub, 0, 0, NULL);
    def_prop = nm;
    while (read_line(k->fp, line, (int)sizeof(line))) {
      const char *r = line;
      if (!next_word(&r, x, 32)) continue;
      if (ieq(x, ":EndProperty")) { ended = 1; break; }
      if (ieq(x, "nabla") || ieq(x, "edit")) {
        int save = interactive;
        interactive = 0;
        enter_editor(r, k->fp);
        interactive = save;
      } else if (x[0] != '#' && !ieq(x, "lamp"))
        printf("apl: only nabla definitions are allowed inside :Property\n");
    }
    def_prop = NULL;
    if (!ended) rt_error("SYNTAX ERROR: :Property without :EndProperty");
    return;
  }
  if (ieq(w, ":Include")) {
    char nm[64];
    Scope *ns;
    int i, n0 = nfns;
    if (!next_word(&rest, nm, 64)) rt_error("SYNTAX ERROR: :Include needs a namespace");
    ns = decl_lookup(nm);
    if (!ns || ns->kind != SK_NS) rt_error_s("VALUE ERROR: no such namespace: ", nm, "");
    for (i = 0; i < n0; i++) {
      Fn *f = fns[i];
      if (f->sc == ns && !f->lam) {
        char key[KEYMAX];
        Fn *c = (Fn *)xmalloc(sizeof(Fn));
        int j;
        kcat(key, cls->pfx, f->name);
        *c = *f;
        c->name = xstrdup(f->name);
        c->key = xstrdup(key);
        c->res = f->res ? xstrdup(f->res) : NULL;
        c->left = f->left ? xstrdup(f->left) : NULL;
        c->right = f->right ? xstrdup(f->right) : NULL;
        c->hdr = xstrdup(f->hdr);
        c->loc = (char **)xmalloc(sizeof(char *) * (size_t)(f->nloc + 1));
        for (j = 0; j < f->nloc; j++) c->loc[j] = xstrdup(f->loc[j]);
        c->lines = (char **)xmalloc(sizeof(char *) * (size_t)(f->nlines + 1));
        c->tk = (Tok **)xmalloc(sizeof(Tok *) * (size_t)(f->nlines + 1));
        c->label = (char **)xmalloc(sizeof(char *) * (size_t)(f->nlines + 1));
        c->lines[0] = NULL; c->tk[0] = NULL; c->label[0] = NULL;
        for (j = 1; j <= f->nlines; j++) {
          c->lines[j] = xstrdup(f->lines[j]);
          c->tk[j] = NULL;
          c->label[j] = f->label[j] ? xstrdup(f->label[j]) : NULL;
        }
        c->nstmt = 0; c->stk = NULL; c->optok = NULL;
        c->cbuilt = 0; c->hasctl = 0; c->ck = NULL; c->cj = NULL;
        c->sc = cls; c->mflag = F_PUBLIC; c->master = NULL; c->bsc = NULL;
        fn_install(c);
      }
    }
    return;
  }
  rt_error_s("SYNTAX ERROR: unknown statement in a class: ", w, "");
}

/* checks and shared-field initialisation once the body is complete */
static void class_finish(void *a)
{
  Scope *cls = (Scope *)a;
  int i, j;
  /* overrides */
  for (i = 0; i < nfns; i++) {
    Fn *f = fns[i];
    Scope *d;
    if (f->sc != cls || f->lam) continue;
    if (f->mflag & (F_CTOR | F_DTOR | F_PGET | F_PSET)) continue;
    if (cls->base && chain_method(cls->base, f->name, &d)) {
      if (!(f->mflag & F_OVER))
        rt_error_s("METHOD ERROR: base method overridden without :Override: ", f->name, "");
    } else if (f->mflag & F_OVER)
      rt_error_s("METHOD ERROR: :Override but no base method to override: ", f->name, "");
  }
  /* interfaces */
  for (i = 0; i < cls->nif; i++) {
    Scope *ifc = cls->ifs[i];
    for (j = 0; j < ifc->nmem; j++) {
      Member *m = &ifc->mem[j];
      Scope *d;
      char buf[200];
      if (m->kind == M_METHOD) {
        Fn *f = chain_method(cls, m->name, &d);
        if (!f || !(f->mflag & F_PUBLIC)) {
          sprintf(buf, "INTERFACE ERROR: %.60s must have a public method %.60s",
                  cls->name, m->name);
          rt_error(buf);
        }
      } else if (m->kind == M_PROP) {
        Member *p = chain_member(cls, m->name, &d);
        if (!p || p->kind != M_PROP || !p->pub) {
          sprintf(buf, "INTERFACE ERROR: %.60s must have a public property %.60s",
                  cls->name, m->name);
          rt_error(buf);
        }
      } else if (m->kind == M_EVENT) {
        Member *p = chain_member(cls, m->name, &d);
        if (!p || p->kind != M_EVENT) {
          sprintf(buf, "INTERFACE ERROR: %.60s must declare event %.60s",
                  cls->name, m->name);
          rt_error(buf);
        }
      }
    }
  }
  /* shared fields */
  for (i = 0; i < cls->nmem; i++) {
    Member *m = &cls->mem[i];
    if (m->kind == M_FIELD && m->shared && m->init) {
      Arr *v;
      scope_push(cls, cls);
      v = eval_text(m->init);
      if (!v) rt_error_s("SYNTAX ERROR: no value for field ", m->name, "");
      var_set(m->name, v);
      scope_pop();
    }
  }
}

typedef struct BlkCtx { FILE *fp; const char *line; int mode; int body; int abort; } BlkCtx;

static void blk_class(BlkCtx *b, const char *rest)
{
  char nm[64], w[64], full[KEYMAX];
  Scope *cls, *base = NULL, *ifs[16];
  int nif = 0, i;
  ClsCtx *k;
  if (!next_word(&rest, nm, 64) || !valid_dotted(nm))
    rt_error("SYNTAX ERROR: :Class needs a name");
  qualify(nm, full);
  while (next_word(&rest, w, 64)) {
    if (ieq(w, ":Extends")) {
      if (!next_word(&rest, w, 64)) rt_error("SYNTAX ERROR: :Extends needs a class");
      base = decl_lookup(w);
      if (!base || base->kind != SK_CLASS)
        rt_error_s("VALUE ERROR: no such class: ", w, "");
    } else if (ieq(w, ":Implements")) {
      const char *save;
      for (;;) {
        Scope *ifc;
        save = rest;
        if (!next_word(&rest, w, 64)) break;
        if (w[0] == ':') { rest = save; break; }
        ifc = decl_lookup(w);
        if (!ifc || ifc->kind != SK_IFACE)
          rt_error_s("VALUE ERROR: no such interface: ", w, "");
        if (nif < 16) ifs[nif++] = ifc;
      }
    } else rt_error_s("SYNTAX ERROR: unexpected in :Class header: ", w, "");
  }
  cls = scope_find_decl(full);
  if (cls) {
    if (cls->kind != SK_CLASS) rt_error_s("DOMAIN ERROR: name is not a class: ", full, "");
    if (base == cls) rt_error("DOMAIN ERROR: a class cannot extend itself");
    if (oo_count_instances(cls))
      rt_error_s("DOMAIN ERROR: class has live instances; cannot redefine: ", full, "");
    class_reset(cls);
  } else {
    if (strchr(full, '.')) {
      char par[KEYMAX], *dot;
      strcpy(par, full);
      dot = strrchr(par, '.');
      *dot = '\0';
      ns_create(par);
    }
    if (fn_find_key(full) || (var_find(full, 0) && var_find(full, 0)->val))
      rt_error_s("DOMAIN ERROR: name already in use: ", full, "");
    cls = scope_new(SK_CLASS, full);
  }
  cls->base = base;
  if (nif > 0) {
    cls->ifs = (Scope **)xmalloc(sizeof(Scope *) * (size_t)nif);
    for (i = 0; i < nif; i++) cls->ifs[i] = ifs[i];
    cls->nif = nif;
  }
  k = (ClsCtx *)xmalloc(sizeof(ClsCtx));
  trk_add(k, 0);
  k->fp = b->fp; k->cls = cls; k->done = 0;
  b->body = 1;
  def_cls = cls;
  def_flags = 0;
  def_prop = NULL;
  while (!k->done) {
    if (!read_line(b->fp, k->line, (int)sizeof(k->line))) {
      def_cls = NULL;
      trk_del(k);
      free(k);
      rt_error("SYNTAX ERROR: :Class without :EndClass");
    }
    guarded(class_line, k);
    def_prop = NULL;
  }
  def_cls = NULL;
  def_flags = 0;
  trk_del(k);
  free(k);
  if (guarded(class_finish, cls)) {
    printf("apl: class %s was not completed and has been removed\n", cls->name);
    class_reset(cls);
    scope_expunge(cls);
    return;
  }
  printf("     class %s defined.\n", cls->name);
}

/* ---------- :Interface ---------- */
static void blk_iface(BlkCtx *b, const char *rest)
{
  char nm[64], line[MAXLINE], w[64], full[KEYMAX];
  Scope *ifc;
  const char *r;
  if (!next_word(&rest, nm, 64) || !valid_dotted(nm))
    rt_error("SYNTAX ERROR: :Interface needs a name");
  qualify(nm, full);
  ifc = scope_find_decl(full);
  if (ifc) {
    int i;
    if (ifc->kind != SK_IFACE) rt_error_s("DOMAIN ERROR: name is not an interface: ", full, "");
    for (i = 0; i < ifc->nmem; i++) { free(ifc->mem[i].name); free(ifc->mem[i].init); }
    free(ifc->mem); ifc->mem = NULL; ifc->nmem = 0;
  } else {
    if (strchr(full, '.')) {
      char par[KEYMAX], *dot;
      strcpy(par, full);
      dot = strrchr(par, '.');
      *dot = '\0';
      ns_create(par);
    }
    ifc = scope_new(SK_IFACE, full);
  }
  b->body = 1;
  while (read_line(b->fp, line, (int)sizeof(line))) {
    int kind = 0;
    r = line;
    if (!next_word(&r, w, 64)) continue;
    if (w[0] == '#' || ieq(w, "lamp")) continue;
    if (ieq(w, ":EndInterface")) {
      printf("     interface %s defined.\n", ifc->name);
      return;
    }
    if (ieq(w, ":Method")) kind = M_METHOD;
    else if (ieq(w, ":Property")) kind = M_PROP;
    else if (ieq(w, ":Event")) kind = M_EVENT;
    else { printf("apl: unknown statement in an interface: %s\n", w); continue; }
    if (!next_word(&r, w, 64) || !valid_ident(w)) {
      printf("apl: interface member needs a name\n");
      continue;
    }
    ifc->mem = (Member *)xrealloc(ifc->mem, sizeof(Member) * (size_t)(ifc->nmem + 1));
    ifc->mem[ifc->nmem].name = xstrdup(w);
    ifc->mem[ifc->nmem].kind = kind;
    ifc->mem[ifc->nmem].pub = 1;
    ifc->mem[ifc->nmem].shared = ifc->mem[ifc->nmem].ro = 0;
    ifc->mem[ifc->nmem].init = NULL;
    ifc->nmem++;
  }
  rt_error("SYNTAX ERROR: :Interface without :EndInterface");
}

/* ---------- :Namespace and :With ---------- */
static void blk_ns(BlkCtx *b, const char *rest)
{
  char nm[128], full[KEYMAX];
  Scope *ns, *save = cur_scope, *sowner = cur_owner;
  if (!next_word(&rest, nm, 128) || !valid_dotted(nm))
    rt_error("SYNTAX ERROR: :Namespace needs a name");
  qualify(nm, full);
  ns = ns_create(full);
  b->body = 1;
  cur_scope = ns;
  cur_owner = NULL;
  if (run_stream_until(b->fp, b->mode, ":EndNamespace")) b->abort = 1;
  b->body = 2;
  cur_scope = save;
  cur_owner = sowner;
}

static void blk_with(BlkCtx *b, const char *rest)
{
  Arr *v;
  Scope *sc, *save = cur_scope, *sowner = cur_owner;
  while (is_space((unsigned char)*rest)) rest++;
  if (*rest == '\0') rt_error("SYNTAX ERROR: :With needs a namespace or object");
  v = eval_text(rest);
  if (!v) rt_error("SYNTAX ERROR: :With needs a value");
  sc = obj_of(v);
  if (!sc || !sc->alive || (sc->kind != SK_NS && sc->kind != SK_INST)) {
    a_rel(v);
    rt_error("DOMAIN ERROR: :With needs a namespace or an instance");
  }
  b->body = 1;
  cur_scope = (sc->id == 0) ? NULL : sc;
  cur_owner = NULL;
  if (run_stream_until(b->fp, b->mode, ":EndWith")) b->abort = 1;
  b->body = 2;
  cur_scope = save;
  cur_owner = sowner;
  a_rel(v);
}

static int oo_is_block(const char *first)
{
  return ieq(first, ":Class") || ieq(first, ":Interface") ||
         ieq(first, ":Namespace") || ieq(first, ":With");
}

static void blk_dispatch(void *a)
{
  BlkCtx *b = (BlkCtx *)a;
  const char *rest = b->line;
  char w[32];
  next_word(&rest, w, 32);
  if (ieq(w, ":Class")) blk_class(b, rest);
  else if (ieq(w, ":Interface")) blk_iface(b, rest);
  else if (ieq(w, ":Namespace")) blk_ns(b, rest);
  else blk_with(b, rest);
}

/* run one block statement starting at `line`; the body is read from fp */
static int oo_block(FILE *fp, const char *line, int mode)
{
  BlkCtx b;
  char w[32];
  const char *rest = line;
  int err;
  b.fp = fp; b.line = line; b.mode = mode; b.body = 0; b.abort = 0;
  err = guarded(blk_dispatch, &b);
  if (b.abort) err = 1;
  if (err && b.body == 0) {
    /* the header was bad: do not run the body as statements */
    next_word(&rest, w, 32);
    if (ieq(w, ":Class")) skip_block(fp, ":EndClass");
    else if (ieq(w, ":Interface")) skip_block(fp, ":EndInterface");
    else if (ieq(w, ":Namespace")) skip_block(fp, ":EndNamespace");
    else skip_block(fp, ":EndWith");
  }
  def_cls = NULL; def_prop = NULL; def_flags = 0; edit_target = NULL;
  return err;
}

/* ---------- expunging ---------- */
static void scope_expunge(Scope *s)
{
  int i;
  if (cur_scope == s) cur_scope = NULL;
  vars_drop_prefix(s->pfx);
  fns_drop_scope(s);
  s->alive = 0;
  for (i = 0; i < ndecls; i++)
    if (decls[i] == s) { decls[i] = decls[--ndecls]; break; }
  if (s->refc <= 0 && !s->inq) scope_free(s);
}

/* []EX of a namespace, class or interface */
static void oo_expunge(Scope *s)
{
  int i;
  if (s->kind == SK_CLASS) {
    if (oo_count_instances(s))
      rt_error_s("DOMAIN ERROR: class has live instances: ", s->name, "");
  }
  for (i = 0; i < ndecls; i++)
    if (decls[i]->kind == SK_CLASS && decls[i]->base == s)
      rt_error_s("DOMAIN ERROR: class is the base of another class: ", s->name, "");
  if (s->kind == SK_NS) {
    /* nested declarations go with it */
    for (i = ndecls - 1; i >= 0; i--) {
      Scope *d;
      if (i >= ndecls) continue;
      d = decls[i];
      if (d != s && strncmp(d->name, s->pfx, strlen(s->pfx)) == 0) {
        if (d->kind == SK_CLASS && oo_count_instances(d))
          rt_error_s("DOMAIN ERROR: class has live instances: ", d->name, "");
      }
    }
    for (i = ndecls - 1; i >= 0; i--) {
      Scope *d;
      if (i >= ndecls) continue;
      d = decls[i];
      if (d != s && strncmp(d->name, s->pfx, strlen(s->pfx)) == 0)
        scope_expunge(d);
    }
  }
  scope_expunge(s);
}

/* )CLEAR: run destructors, then drop every object */
static void oo_clear_all(void)
{
  oo_destroy_all();
}
static void oo_clear_finish(void)
{
  int i;
  oo_flush();
  while (ndecls > 0) scope_expunge(decls[0]);
  for (i = 0; i < ap_n; ) {
    if (ap_tab[i].objid) {
      a_rel(ap_tab[i].oref);
      for (; i + 1 < ap_n; i++) ap_tab[i] = ap_tab[i + 1];
      ap_n--;
      i = 0;
    } else i++;
  }
  a_rel(prop_tmp);
  prop_tmp = NULL;
  cur_scope = NULL;
  cur_owner = NULL;
}

/* ---------- names, classes of names ---------- */
static double oo_nc(const char *nm)
{
  Res r;
  Fn *f;
  Arr *v;
  if (!cur_scope && !strchr(nm, '.')) {
    f = fn_find_key(nm);
    if (f) return f->opn ? 4.0 : 3.0;
  }
  if (oo_find(nm, 0, &r)) {
    if (r.kind == R_FN) return (r.fn->opn) ? 4.0 : 3.0;
    if (r.kind == R_SCOPE) {
      switch (r.sc->kind) {
      case SK_NS: return 9.1;
      case SK_CLASS: return 9.4;
      case SK_IFACE: return 9.5;
      default: return 9.2;
      }
    }
    if (r.kind == R_PROP) return 2.0;
    if (r.kind == R_VAR) {
      Var *var = var_find(r.key, 0);
      if (!var) return 0.0;
      v = var_value(var);
      if (!v) return 0.0;
      if (obj_of(v)) {
        Scope *o = obj_of(v);
        return o->kind == SK_INST ? 9.2 : (o->kind == SK_NS ? 9.1 :
               (o->kind == SK_CLASS ? 9.4 : 9.5));
      }
      return 2.0;
    }
  }
  return 0.0;
}

/* []EX for names that are not variables or functions */
static int oo_ex(const char *nm)
{
  Res r;
  if (oo_find(nm, 0, &r) && r.kind == R_SCOPE &&
      r.sc->kind != SK_INST && r.sc->id > 0) {
    oo_expunge(r.sc);
    return 1;
  }
  return 0;
}

/* ---------- []NL ---------- */
typedef struct NameList { char **v; int n; } NameList;
static void nl_add(NameList *l, const char *s)
{
  int i;
  for (i = 0; i < l->n; i++) if (strcmp(l->v[i], s) == 0) return;
  l->v = (char **)xrealloc(l->v, sizeof(char *) * (size_t)(l->n + 1));
  l->v[l->n++] = xstrdup(s);
}
static int code_match(double want, double have)
{
  if (fabs(want - have) < 1e-9) return 1;
  if (fabs(want - 9.0) < 1e-9 && have > 9.0 && have < 10.0) return 1;
  return 0;
}
static double scope_code(Scope *o)
{
  switch (o->kind) {
  case SK_NS: return 9.1;
  case SK_CLASS: return 9.4;
  case SK_IFACE: return 9.5;
  default: return 9.2;
  }
}
static void nl_scan(Scope *X, double code, NameList *l)
{
  int i, j, root;
  size_t pl;
  const char *pfx;
  Var *v;
  if (!X) X = cur_scope ? cur_scope : root_scope();
  if (X->kind == SK_INST || X->kind == SK_CLASS) {
    Scope *c;
    for (c = (X->kind == SK_INST ? X->cls : X); c; c = c->base) {
      for (j = 0; j < c->nmem; j++) {
        Member *m = &c->mem[j];
        if (!(m->pub || oo_restoring || cur_owner == c)) continue;
        if (m->kind == M_FIELD || m->kind == M_PROP) {
          int ok = 1;
          if (X->kind == SK_CLASS && !m->shared) ok = 0;
          if (ok && code_match(code, 2.0)) nl_add(l, m->name);
        }
      }
      for (i = 0; i < nfns; i++) {
        Fn *f = fns[i];
        if (f->sc != c || (f->mflag & (F_PGET | F_PSET))) continue;
        if (!(f->mflag & F_PUBLIC) && !oo_restoring && cur_owner != c) continue;
        if (X->kind == SK_CLASS && !(f->mflag & F_SHARED)) continue;
        if (code_match(code, 3.0)) nl_add(l, f->name);
      }
    }
    return;
  }
  root = (X->id == 0);
  pfx = X->pfx;
  pl = strlen(pfx);
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next) {
      const char *rest;
      double c;
      if (!v->val && !v->ap) continue;
      if (root) {
        if (strchr(v->name, '.') || v->name[0] == '\1') continue;
        rest = v->name;
      } else {
        if (strncmp(v->name, pfx, pl) != 0) continue;
        rest = v->name + pl;
        if (strchr(rest, '.')) continue;
      }
      if (v->val && obj_of(v->val)) c = scope_code(obj_of(v->val));
      else c = 2.0;
      if (code_match(code, c)) nl_add(l, rest);
    }
  for (i = 0; i < nfns; i++) {
    Fn *f = fns[i];
    const char *rest;
    if (root) {
      if (strchr(f->key, '.')) continue;
      rest = f->key;
    } else {
      if (strncmp(f->key, pfx, pl) != 0) continue;
      rest = f->key + pl;
      if (strchr(rest, '.')) continue;
    }
    if (code_match(code, f->opn ? 4.0 : 3.0)) nl_add(l, rest);
  }
  for (i = 0; i < ndecls; i++) {
    Scope *d = decls[i];
    const char *rest;
    if (!d->alive) continue;
    if (root) {
      if (strchr(d->name, '.')) continue;
      rest = d->name;
    } else {
      if (strncmp(d->name, pfx, pl) != 0) continue;
      rest = d->name + pl;
      if (strchr(rest, '.')) continue;
    }
    if (code_match(code, scope_code(d))) nl_add(l, rest);
  }
}

static int nl_cmp(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static Arr *nl_list(Scope *X, Arr *codes)
{
  NameList l;
  int i, w = 0, sh[2];
  Arr *r;
  l.v = NULL; l.n = 0;
  if (codes->type != T_NUM) rt_error("DOMAIN ERROR: []NL needs name-class numbers");
  for (i = 0; i < codes->n; i++) nl_scan(X, codes->num[i], &l);
  if (l.n > 1) qsort(l.v, (size_t)l.n, sizeof(char *), nl_cmp);
  for (i = 0; i < l.n; i++) if ((int)strlen(l.v[i]) > w) w = (int)strlen(l.v[i]);
  sh[0] = l.n; sh[1] = w;
  r = a_new(T_CHR, 2, sh);
  if (l.n * w > 0) memset(r->chr, ' ', (size_t)(l.n * w));
  for (i = 0; i < l.n; i++) {
    memcpy(r->chr + i * w, l.v[i], strlen(l.v[i]));
    free(l.v[i]);
  }
  free(l.v);
  return r;
}

/* ---------- object-handling system functions ---------- */
/* items from..n-1 of the nested vector x as a single argument (or NULL) */
static Arr *items_from(Arr *x, int from)
{
  int n = x->n - from, i;
  Arr *r;
  if (x->type != T_NEST || n <= 0) return NULL;
  if (n == 1) return a_item(x, from);
  r = a_new(T_NEST, 1, &n);
  for (i = 0; i < n; i++) a_put(r, i, a_item(x, from + i));
  return a_simplify(r);
}

static Scope *class_arg(Arr *x, const char *who)
{
  Scope *c = obj_of(x);
  if (!c && x->type == T_CHR && x->rank <= 1) {
    char *t = text_of(x, who);
    trk_add(t, 0);
    c = decl_lookup(t);
    trk_del(t);
    free(t);
  }
  return c;
}

static Arr *q_new(Arr *x)
{
  Scope *c;
  if (x->type == T_NEST && x->rank == 1 && x->n >= 1 && obj_of(x->sub[0])) {
    Arr *args = items_from(x, 1), *r;
    c = obj_of(x->sub[0]);
    r = oo_new(c, args);
    if (args) a_rel(args);
    return r;
  }
  c = class_arg(x, "[]NEW");
  if (!c) rt_error("DOMAIN ERROR: []NEW needs a class (and its arguments)");
  return oo_new(c, NULL);
}

static Arr *q_ns(Arr *x)
{
  char *t, full[KEYMAX];
  Scope *s;
  if (obj_of(x)) return a_ref(x);
  t = text_of(x, "[]NS");
  trk_add(t, 0);
  if (t[0] == '\0') {
    s = scope_new(SK_NS, "");
  } else {
    if (!valid_dotted(t)) rt_error_s("DOMAIN ERROR: bad namespace name: ", t, "");
    qualify(t, full);
    s = ns_create(full);
  }
  trk_del(t);
  free(t);
  return obj_ref(s);
}

static Scope *ns_arg(Arr *x, const char *who)
{
  Scope *s = obj_of(x);
  if (!s && x->type == T_CHR && x->rank <= 1) {
    char *t = text_of(x, who);
    trk_add(t, 0);
    if (t[0] == '\0' || strcmp(t, "#") == 0) s = root_scope();
    else s = decl_lookup(t);
    trk_del(t);
    free(t);
  }
  if (!s || !s->alive || s->kind != SK_NS) rt_error("DOMAIN ERROR: not a namespace");
  return s;
}

static Arr *q_cs(Arr *x)
{
  Arr *old = obj_ref(cur_scope ? cur_scope : root_scope());
  Scope *s = ns_arg(x, "[]CS");
  cur_scope = (s->id == 0) ? NULL : s;
  cur_owner = NULL;
  return old;
}

static Arr *q_class(Arr *x)
{
  Scope *s = obj_of(x);
  if (!s) rt_error("DOMAIN ERROR: []CLASS needs an object");
  if (s->kind == SK_INST) return obj_ref(s->cls);
  if (s->kind == SK_CLASS && s->base) return obj_ref(s->base);
  return a_vec(T_NUM, 0);
}

static Arr *q_instances(Arr *x)
{
  Scope *c = class_arg(x, "[]INSTANCES");
  int i, n = 0, k = 0;
  Arr *r;
  if (!c || c->kind != SK_CLASS) rt_error("DOMAIN ERROR: []INSTANCES needs a class");
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_INST && !s->dying && is_derived(s->cls, c)) n++;
  }
  r = a_new(T_NEST, 1, &n);
  for (i = 1; i < nscopes && k < n; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_INST && !s->dying && is_derived(s->cls, c))
      a_put(r, k++, obj_ref(s));
  }
  if (n == 0) { a_rel(r); return a_vec(T_NUM, 0); }
  return r;
}

static Arr *q_nq(Arr *x)
{
  Scope *o;
  char *ev;
  Arr *data;
  if (x->type != T_NEST || x->rank != 1 || x->n < 2 || !(o = obj_of(x->sub[0])) ||
      x->sub[1]->type != T_CHR)
    rt_error("DOMAIN ERROR: []NQ needs  object 'Event' [data]");
  if (o->kind != SK_INST) rt_error("DOMAIN ERROR: events are raised on instances");
  ev = text_of(x->sub[1], "[]NQ");
  trk_add(ev, 0);
  data = items_from(x, 2);
  oo_raise(o, ev, data);
  if (data) a_rel(data);
  trk_del(ev);
  free(ev);
  return a_scalar(1.0);
}

static void idm_clear(void)
{
  int i;
  for (i = 0; i < idm_n; i++) a_rel(idm_ref[i]);
  free(idm_ref); free(idm_id);
  idm_ref = NULL; idm_id = NULL; idm_n = 0;
}
static Arr *q_obj(Arr *x)
{
  Scope *c, *inst;
  Arr *ref;
  int id;
  if (x->type != T_NEST || x->n != 2 || x->sub[0]->type != T_NUM ||
      x->sub[1]->type != T_CHR)
    rt_error("DOMAIN ERROR: []OBJ needs  id 'Class'");
  id = (int)x->sub[0]->num[0];
  if (x->sub[1]->n == 0) inst = scope_new(SK_NS, "");      /* anonymous */
  else {
    c = class_arg(x->sub[1], "[]OBJ");
    if (!c || c->kind != SK_CLASS) rt_error("VALUE ERROR: []OBJ: no such class");
    inst = scope_new(SK_INST, c->name);
    inst->cls = c;
  }
  ref = obj_ref(inst);
  idm_ref = (Arr **)xrealloc(idm_ref, sizeof(Arr *) * (size_t)(idm_n + 1));
  idm_id = (int *)xrealloc(idm_id, sizeof(int) * (size_t)(idm_n + 1));
  idm_ref[idm_n] = a_ref(ref);
  idm_id[idm_n] = id;
  idm_n++;
  return ref;
}
static Arr *q_objref(Arr *x)
{
  int i;
  if (x->type == T_NUM && x->n == 1) {
    int id = (int)x->num[0];
    for (i = 0; i < idm_n; i++)
      if (idm_id[i] == id) return a_ref(idm_ref[i]);
    rt_error("VALUE ERROR: []OBJREF: unknown object");
  }
  {
    Scope *s = class_arg(x, "[]OBJREF");
    if (!s) {
      char *t = text_of(x, "[]OBJREF");
      int root = (strcmp(t, "#") == 0);
      free(t);
      if (root) return obj_ref(root_scope());
      rt_error("VALUE ERROR: []OBJREF: no such name");
    }
    return obj_ref(s);
  }
}

static int oo_quad_is_fn(const char *name)
{
  static const char *const names[] = {
    "NEW", "NS", "CS", "NL", "CLASS", "INSTANCES", "DF", "NQ", "NR", "FIX",
    "OBJ", "OBJREF", NULL };
  int i;
  for (i = 0; names[i]; i++) if (strcmp(name, names[i]) == 0) return 1;
  return 0;
}

static Arr *fix_script(Arr *x);
static Arr *nr_define(Arr *x);

/* monadic system functions of the object layer; 1 if handled */
static int oo_quad_fn(const char *name, Arr *x, Arr **out)
{
  if (strcmp(name, "NEW") == 0) { *out = q_new(x); return 1; }
  if (strcmp(name, "NS") == 0) { *out = q_ns(x); return 1; }
  if (strcmp(name, "CS") == 0) { *out = q_cs(x); return 1; }
  if (strcmp(name, "CLASS") == 0) { *out = q_class(x); return 1; }
  if (strcmp(name, "INSTANCES") == 0) { *out = q_instances(x); return 1; }
  if (strcmp(name, "NQ") == 0) { *out = q_nq(x); return 1; }
  if (strcmp(name, "OBJ") == 0) { *out = q_obj(x); return 1; }
  if (strcmp(name, "OBJREF") == 0) { *out = q_objref(x); return 1; }
  if (strcmp(name, "NL") == 0) { *out = nl_list(NULL, x); return 1; }
  if (strcmp(name, "FIX") == 0) { *out = fix_script(x); return 1; }
  if (strcmp(name, "NR") == 0) { *out = nr_define(x); return 1; }
  if (strcmp(name, "DF") == 0) {
    Scope *o = obj_of(x);
    if (!o) rt_error("DOMAIN ERROR: []DF needs an object");
    *out = a_str(obj_text(o));
    return 1;
  }
  return 0;
}

static int oo_quad_dya(const char *name)
{
  return strcmp(name, "ON") == 0 || strcmp(name, "ISA") == 0 ||
         strcmp(name, "DF") == 0 || strcmp(name, "NL") == 0 ||
         strcmp(name, "PROC") == 0;
}

static Arr *quad_dya(const char *name, Arr *x, Arr *y)
{
  if (strcmp(name, "ON") == 0) {
    Scope *o = obj_of(x);
    char *ev, *path;
    Scope *save = cur_scope;
    if (!o || o->kind != SK_INST) rt_error("DOMAIN ERROR: []ON needs an instance on the left");
    if (y->type != T_NEST || y->rank != 1 || y->n < 2 || y->n > 3 ||
        y->sub[0]->type != T_CHR || y->sub[1]->type != T_CHR)
      rt_error("DOMAIN ERROR: []ON needs  'Event' 'Handler'");
    ev = text_of(y->sub[0], "[]ON");
    trk_add(ev, 0);
    path = text_of(y->sub[1], "[]ON");
    trk_add(path, 0);
    if (y->n == 3) {
      Scope *h = obj_of(y->sub[2]);
      if (!h) rt_error("DOMAIN ERROR: []ON: third item must be an object");
      cur_scope = (h->id == 0) ? NULL : h;
    }
    oo_on(o, ev, path);
    cur_scope = save;
    trk_del(path); trk_del(ev);
    free(path); free(ev);
    return a_ref(x);
  }
  if (strcmp(name, "ISA") == 0) {
    Scope *o = obj_of(x), *t = class_arg(y, "[]ISA");
    if (!o) rt_error("DOMAIN ERROR: []ISA needs an object on the left");
    if (!t) return a_scalar(0.0);
    if (o->kind == SK_INST) return a_scalar(class_isa(o->cls, t) ? 1.0 : 0.0);
    if (o->kind == SK_CLASS) return a_scalar(class_isa(o, t) ? 1.0 : 0.0);
    return a_scalar(0.0);
  }
  if (strcmp(name, "DF") == 0) {
    Scope *o = obj_of(x);
    char *t;
    if (!o || o->kind != SK_INST) rt_error("DOMAIN ERROR: []DF needs an instance on the left");
    t = text_of(y, "[]DF");
    free(o->df);
    o->df = t;
    return a_ref(x);
  }
  if (strcmp(name, "NL") == 0) {
    Scope *o = obj_of(x);
    if (!o) rt_error("DOMAIN ERROR: []NL needs a namespace or object on the left");
    return nl_list(o, y);
  }
  if (strcmp(name, "PROC") == 0) {
    Scope *o = obj_of(y);
    int id;
    if (x->type != T_NUM || x->n != 1) rt_error("DOMAIN ERROR: []PROC needs a processor number on the left");
    id = as_int(x->num[0], P_INDEX);
    if (id < 1) rt_error("DOMAIN ERROR: bad processor number");
    ap_for_object(o, id);
    return a_scalar((double)id);
  }
  rt_error("SYNTAX ERROR: unknown system function");
  return NULL;
}

/* obj.[]NL x  and the like: run the system function inside obj */
static Arr *oo_quad_in(const char *path, Arr *x)
{
  Res r;
  Arr *res = NULL;
  if (!oo_find(path, 0, &r) || r.kind != R_QUAD)
    rt_error_s("VALUE ERROR: no such object or system function: ", path, "");
  if (strcmp(r.qname, "NL") == 0) return nl_list(r.sc, x);
  scope_push(r.sc, NULL);
  res = quad_fn(r.qname, x);
  scope_pop();
  return res;
}

/* ---------- []FIX and []NR ---------- */
static Arr *fix_script(Arr *x)
{
  int nrows, i;
  char **rows;
  const char *r;
  char w[64], nm[128];
  FILE *tf;
  if (x->type == T_NEST) {
    nrows = x->n;
    rows = (char **)xmalloc(sizeof(char *) * (size_t)(nrows ? nrows : 1));
    for (i = 0; i < nrows; i++) {
      Arr *it = x->sub[i];
      if (it->type != T_CHR || it->rank > 1) {
        while (--i >= 0) free(rows[i]);
        free(rows);
        rt_error("DOMAIN ERROR: []FIX needs text rows");
      }
      rows[i] = xstrndup(it->chr, it->n);
    }
  } else {
    nrows = text_rows(x);
    rows = (char **)xmalloc(sizeof(char *) * (size_t)nrows);
    for (i = 0; i < nrows; i++) rows[i] = text_row(x, i, nrows);
  }
  if (nrows < 1) { free(rows); rt_error("DOMAIN ERROR: []FIX needs the lines of a definition"); }
  r = rows[0];
  while (is_space((unsigned char)*r)) r++;
  if (*r != ':') {
    for (i = 0; i < nrows; i++) free(rows[i]);
    free(rows);
    return fx_define(x);
  }
  r = rows[0];
  next_word(&r, w, 64);
  nm[0] = '\0';
  next_word(&r, nm, 128);
  tf = tmpfile();
  if (!tf) {
    for (i = 0; i < nrows; i++) free(rows[i]);
    free(rows);
    rt_error("DOMAIN ERROR: []FIX: cannot create a temporary file");
  }
  for (i = 0; i < nrows; i++) { fprintf(tf, "%s\n", rows[i]); free(rows[i]); }
  free(rows);
  rewind(tf);
  {
    Scope *save = cur_scope;
    int sv = catch_top;
    run_stream(tf, 1);
    catch_top = sv;
    cur_scope = save;
  }
  fclose(tf);
  return a_str(nm);
}

static Arr *nr_define(Arr *x)
{
  Arr *m = quad_fn("CR", x), *r;
  int rows, w, i, n;
  if (m->rank != 2) return m;
  rows = m->shape[0]; w = m->shape[1];
  r = a_new(T_NEST, 1, &rows);
  for (i = 0; i < rows; i++) {
    n = w;
    while (n > 0 && m->chr[i * w + n - 1] == ' ') n--;
    {
      Arr *line = a_vec(T_CHR, n);
      memcpy(line->chr, m->chr + i * w, (size_t)n);
      a_put(r, i, line);
    }
  }
  a_rel(m);
  return r;
}

/* ---------- )CS )CLASSES )NSS ---------- */
static void sys_cs(const char *arg)
{
  char *a = trim_dup(arg);
  if (a[0] == '\0' || strcmp(a, "#") == 0) {
    cur_scope = NULL;
    printf("#\n");
  } else {
    Scope *s = decl_lookup(a);
    if (!s || s->kind != SK_NS) printf("apl: no such namespace: %s\n", a);
    else { cur_scope = s; cur_owner = NULL; printf("#.%s\n", s->name); }
  }
  free(a);
}

static void sys_list_kind(int kind, const char *what)
{
  char **names = NULL;
  int n = 0, i;
  for (i = 0; i < ndecls; i++)
    if (decls[i]->alive && decls[i]->kind == kind) {
      names = (char **)xrealloc(names, sizeof(char *) * (size_t)(n + 1));
      names[n++] = decls[i]->name;
    }
  if (n == 0) { printf("(no %s defined)\n", what); free(names); return; }
  qsort(names, (size_t)n, sizeof(char *), cmp_str);
  for (i = 0; i < n; i++) {
    Scope *c = scope_find_decl(names[i]);
    printf("%s%s", i ? " " : "", names[i]);
    if (kind == SK_CLASS && c->base) printf("(:%s)", c->base->name);
  }
  printf("\n");
  free(names);
}
static void sys_classes(void) { sys_list_kind(SK_CLASS, "classes"); }
static void sys_nss(void) { sys_list_kind(SK_NS, "namespaces"); }
static void sys_ifaces(void) { sys_list_kind(SK_IFACE, "interfaces"); }

/* ---------- saving objects ---------- */
static void emit_fn(FILE *fp, const Fn *f)
{
  int j;
  if (f->lam) { fprintf(fp, "%s <- %s\n", f->name, f->hdr); return; }
  fprintf(fp, "nabla %s\n", f->hdr);
  for (j = 1; j <= f->nlines; j++) fprintf(fp, "%s\n", f->lines[j]);
  fprintf(fp, "nabla\n");
}

/* the expression that rebuilds a reference (used by emit_expr) */
static void emit_ref(FILE *fp, Scope *s)
{
  if (s->kind == SK_INST || s->anon) fprintf(fp, "([]OBJREF %d)", s->id);
  else if (s->id == 0) fprintf(fp, "([]OBJREF '#')");
  else fprintf(fp, "([]OBJREF '%s')", s->name);
}

static void save_class(FILE *fp, Scope *c)
{
  int i, j;
  fprintf(fp, ":Class %s", c->name);
  if (c->base) fprintf(fp, " :Extends %s", c->base->name);
  if (c->nif) {
    fprintf(fp, " :Implements");
    for (i = 0; i < c->nif; i++) fprintf(fp, " %s", c->ifs[i]->name);
  }
  fprintf(fp, "\n");
  for (i = 0; i < c->nmem; i++) {
    Member *m = &c->mem[i];
    if (m->kind == M_FIELD) {
      fprintf(fp, ":Field %s%s%s %s", m->pub ? "Public" : "Private",
              m->shared ? " Shared" : "", m->ro ? " ReadOnly" : "", m->name);
      if (m->init) fprintf(fp, " <- %s", m->init);
      fprintf(fp, "\n");
    } else if (m->kind == M_EVENT) fprintf(fp, ":Event %s\n", m->name);
    else if (m->kind == M_PROP) {
      char key[KEYMAX];
      Fn *g, *s;
      fprintf(fp, ":Property %s %s\n", m->pub ? "Public" : "Private", m->name);
      kcat(key, c->pfx, m->name);
      strcat(key, ":get");
      g = fn_find_key(key);
      if (g) emit_fn(fp, g);
      key[strlen(key) - 4] = '\0';
      strcat(key, ":set");
      s = fn_find_key(key);
      if (s) emit_fn(fp, s);
      fprintf(fp, ":EndProperty\n");
    }
  }
  for (j = 0; j < nfns; j++) {
    Fn *f = fns[j];
    if (f->sc != c || (f->mflag & (F_PGET | F_PSET))) continue;
    fprintf(fp, ":Access %s%s\n", (f->mflag & F_PUBLIC) ? "Public" : "Private",
            (f->mflag & F_SHARED) ? " Shared" : "");
    if (f->mflag & F_OVER) fprintf(fp, ":Override\n");
    if (f->mflag & F_CTOR) fprintf(fp, ":Constructor\n");
    if (f->mflag & F_DTOR) fprintf(fp, ":Destructor\n");
    emit_fn(fp, f);
  }
  fprintf(fp, ":EndClass\n");
}

/* interfaces, classes, namespaces (with their functions) */
static void oo_save_defs(FILE *fp, int *ncls, int *nns)
{
  int i, j;
  for (i = 0; i < ndecls; i++) {
    Scope *s = decls[i];
    if (!s->alive || s->kind != SK_IFACE) continue;
    fprintf(fp, ":Interface %s\n", s->name);
    for (j = 0; j < s->nmem; j++)
      fprintf(fp, ":%s %s\n", s->mem[j].kind == M_METHOD ? "Method" :
              (s->mem[j].kind == M_PROP ? "Property" : "Event"), s->mem[j].name);
    fprintf(fp, ":EndInterface\n");
  }
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_CLASS) { save_class(fp, s); (*ncls)++; }
  }
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (s && s->alive && s->kind == SK_NS && !s->anon) {
      fprintf(fp, ":Namespace %s\n", s->name);
      for (j = 0; j < nfns; j++)
        if (fns[j]->sc == s) emit_fn(fp, fns[j]);
      fprintf(fp, ":EndNamespace\n");
      (*nns)++;
    }
  }
}

/* instances (and anonymous namespaces) as bare objects */
static void oo_save_instances(FILE *fp, int *ninst)
{
  int i;
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    if (!s || !s->alive || s->dying) continue;
    if (s->kind == SK_INST)
      fprintf(fp, "loadtmp_ <- []OBJ (%d) ('%s')\n", s->id, s->cls->name);
    else if (s->kind == SK_NS && s->anon)
      fprintf(fp, "loadtmp_ <- []OBJ (%d) (0 rho '')\n", s->id);
    else continue;
    (*ninst)++;
  }
}

/* values held by namespaces, classes (shared fields) and instances */
static void oo_save_values(FILE *fp)
{
  int i, j;
  Var *v;
  char **keys = NULL;
  int nk = 0;
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (v->val && strchr(v->name, '.') && v->name[0] != '\1') {
        keys = (char **)xrealloc(keys, sizeof(char *) * (size_t)(nk + 1));
        keys[nk++] = v->name;
      }
  if (nk > 1) qsort(keys, (size_t)nk, sizeof(char *), cmp_str);
  for (i = 0; i < nk; i++) save_value(fp, keys[i], var_find(keys[i], 0)->val);
  free(keys);
  for (i = 1; i < nscopes; i++) {
    Scope *s = scopes[i];
    int any = 0;
    if (!s || !s->alive || s->dying) continue;
    if (!(s->kind == SK_INST || (s->kind == SK_NS && s->anon))) continue;
    fprintf(fp, "loadtmp_ <- []OBJREF %d\n", s->id);
    for (j = 0; j < s->nvk; j++) {
      Var *var = var_find(s->vk[j], 0);
      char nm[KEYMAX + 16];
      if (!var || !var->val) continue;
      sprintf(nm, "loadtmp_.%s", s->vk[j] + strlen(s->pfx));
      save_value(fp, nm, var->val);
    }
    if (s->df) {
      const char *p;
      fprintf(fp, "loadtmp_ <- loadtmp_ []DF '");
      for (p = s->df; *p; p++) { if (*p == '\'') fputc('\'', fp); fputc(*p, fp); }
      fprintf(fp, "'\n");
    }
    for (j = 0; j < s->nhd; j++) {
      Scope *h = scope_by_id(s->hd[j].home);
      fprintf(fp, "loadtmp_ <- loadtmp_ []ON ('%s') ('%s')", s->hd[j].ev, s->hd[j].path);
      if (h) { fprintf(fp, " "); emit_ref(fp, h); }
      fprintf(fp, "\n");
    }
    for (j = 0; j < nfns; j++)
      if (fns[j]->sc == s) any = 1;
    if (any) {
      fprintf(fp, ":With loadtmp_\n");
      for (j = 0; j < nfns; j++)
        if (fns[j]->sc == s) emit_fn(fp, fns[j]);
      fprintf(fp, ":EndWith\n");
    }
  }
}

/* shared variables and processors */
static void oo_save_procs(FILE *fp, int *nshare)
{
  int i;
  Var *v;
  for (i = 0; i < ap_n; i++)
    if (ap_tab[i].objid)
      fprintf(fp, "loadtmp_ <- %d []PROC ([]OBJREF %d)\n", ap_tab[i].id,
              ap_tab[i].objid);
  for (i = 0; i < HSIZE; i++)
    for (v = vtab[i]; v; v = v->next)
      if (v->ap && !strchr(v->name, '.') && v->name[0] != '\1') {
        fprintf(fp, "loadtmp_ <- %d []SVO '%s'\n", v->ap, v->name);
        (*nshare)++;
      }
}

/* around a workspace load: the root scope, access rules off */
static Scope *load_save_scope;
static Scope *load_save_owner;
static void oo_load_begin(void)
{
  load_save_scope = cur_scope;
  load_save_owner = cur_owner;
  cur_scope = NULL;
  cur_owner = NULL;
  oo_restoring = 1;
}
static void oo_load_end(void)
{
  oo_restoring = 0;
  idm_clear();
  var_set("loadtmp_", NULL);
  cur_scope = load_save_scope;
  cur_owner = load_save_owner;
}

static void flush_thunk(void *a) { (void)a; oo_flush(); }
static void guarded_flush_(void) { guarded(flush_thunk, NULL); }
