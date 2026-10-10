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
