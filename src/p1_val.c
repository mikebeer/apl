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
