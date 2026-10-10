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
