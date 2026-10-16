/* p3c_fmt.c - []FMT : formatted output (APL2 / Dyalog compatible subset)
 *
 *   spec []FMT data      the specification is a text of phrases separated by
 *                        commas; the result is a character matrix
 *   []FMT data           the display of the data as a character matrix
 *
 * Phrases:  Iw  Fw.d  Ew.d  Gtext  Aw  Xw  Tc  'literal'  and a repeat count
 *           n  before a phrase or before a group  n( ... ).
 * Qualifiers before I F E G:  B (blank if zero)  C (thousands commas)
 *           L (left justify)  Z (zeros instead of blanks)  Kn (scale by 10^n)
 *           M'text' (negative: text after the number, no minus sign)
 *           N'text' (negative: text before the number, no minus sign)
 * Data: a matrix is one column per phrase; a vector is one column; a nested
 * vector gives one block per item (a numeric item is a column, a text item is
 * one A field per row).  A phrase list is reused if there are more columns.
 */
#define FM_I 1
#define FM_F 2
#define FM_E 3
#define FM_G 4
#define FM_A 5
#define FM_X 6
#define FM_T 7
#define FM_TEXT 8
#define FM_MAXPH 512

typedef struct FmPh {
  int kind;
  int w, d;
  int bq, cq, lq, zq, kq;
  int hasm, hasn;
  char txt[80];                 /* literal or G pattern */
  char mtxt[24], ntxt[24];      /* M and N texts */
} FmPh;

typedef struct FmSpec {
  FmPh ph[FM_MAXPH];
  int n;
} FmSpec;


/* the text between delimiters q ... q starting after the opening one */
static void fm_quoted(const char *s, int *i, int len, char q, char *out, int max)
{
  int m = 0;
  while (*i < len) {
    if (s[*i] == q) {
      if (*i + 1 < len && s[*i + 1] == q) {
        if (m >= max - 1) rt_error("LIMIT ERROR: []FMT: text in the format is too long");
        out[m++] = q; *i += 2; continue;
      }
      (*i)++;
      out[m] = '\0';
      return;
    }
    if (m >= max - 1) rt_error("LIMIT ERROR: []FMT: text in the format is too long");
    out[m++] = s[(*i)++];
  }
  rt_error("DOMAIN ERROR: []FMT: unterminated text in the format");
}

static int fm_int(const char *s, int *i, int len, int *has)
{
  int v = 0;
  *has = 0;
  while (*i < len && is_digit((unsigned char)s[*i])) {
    v = v * 10 + (s[*i] - '0');
    if (v > 100000) rt_error("DOMAIN ERROR: []FMT: number too large in the format");
    (*i)++;
    *has = 1;
  }
  return v;
}

static void fm_add(FmSpec *sp, const FmPh *p)
{
  if (sp->n >= FM_MAXPH) rt_error("LIMIT ERROR: []FMT: format too long");
  sp->ph[sp->n++] = *p;
}

/* parse phrases s[i..end) into sp; stops at ")" when depth > 0 */
static void fm_parse(const char *s, int *i, int len, FmSpec *sp, int depth)
{
  for (;;) {
    FmPh p;
    int has, rep = 1, c, start;
    while (*i < len && (s[*i] == ' ' || s[*i] == ',')) (*i)++;
    if (*i >= len) {
      if (depth) rt_error("DOMAIN ERROR: []FMT: missing ) in the format");
      return;
    }
    if (s[*i] == ')') {
      if (!depth) rt_error("DOMAIN ERROR: []FMT: unbalanced ) in the format");
      (*i)++;
      return;
    }
    memset(&p, 0, sizeof(p));
    rep = fm_int(s, i, len, &has);
    if (!has) rep = 1;
    if (*i < len && s[*i] == '(') {
      FmSpec *inner = (FmSpec *)xmalloc(sizeof(FmSpec));
      int k, j;
      inner->n = 0;
      trk_add(inner, 0);
      (*i)++;
      fm_parse(s, i, len, inner, depth + 1);
      for (k = 0; k < rep; k++)
        for (j = 0; j < inner->n; j++) fm_add(sp, &inner->ph[j]);
      trk_del(inner);
      free(inner);
      continue;
    }
    if (*i < len && (s[*i] == '\'' || s[*i] == '"')) {
      char q = s[*i];
      (*i)++;
      p.kind = FM_TEXT;
      fm_quoted(s, i, len, q, p.txt, (int)sizeof(p.txt));
      { int k; for (k = 0; k < rep; k++) fm_add(sp, &p); }
      continue;
    }
    /* qualifiers */
    start = 1;
    while (start && *i < len) {
      c = to_upper((unsigned char)s[*i]);
      if (c == ' ') { (*i)++; continue; }
      if (c == 'B') { p.bq = 1; (*i)++; }
      else if (c == 'C') { p.cq = 1; (*i)++; }
      else if (c == 'L') { p.lq = 1; (*i)++; }
      else if (c == 'Z') { p.zq = 1; (*i)++; }
      else if (c == 'K') {
        int neg = 0, v;
        (*i)++;
        if (*i < len && (s[*i] == '-' || s[*i] == '~')) { neg = 1; (*i)++; }
        v = fm_int(s, i, len, &has);
        if (!has) rt_error("DOMAIN ERROR: []FMT: K needs a number");
        p.kq = neg ? -v : v;
      }
      else if (c == 'M' || c == 'N') {
        char q;
        (*i)++;
        if (*i >= len || (s[*i] != '\'' && s[*i] != '"'))
          rt_error("DOMAIN ERROR: []FMT: M and N need quoted text");
        q = s[*i]; (*i)++;
        if (c == 'M') { p.hasm = 1; fm_quoted(s, i, len, q, p.mtxt, (int)sizeof(p.mtxt)); }
        else { p.hasn = 1; fm_quoted(s, i, len, q, p.ntxt, (int)sizeof(p.ntxt)); }
      }
      else start = 0;
    }
    if (*i >= len) rt_error("DOMAIN ERROR: []FMT: phrase has no type letter");
    c = to_upper((unsigned char)s[*i]);
    (*i)++;
    switch (c) {
    case 'I': p.kind = FM_I; break;
    case 'F': p.kind = FM_F; break;
    case 'E': p.kind = FM_E; break;
    case 'G': p.kind = FM_G; break;
    case 'A': p.kind = FM_A; break;
    case 'X': p.kind = FM_X; break;
    case 'T': p.kind = FM_T; break;
    default:
      rt_error("DOMAIN ERROR: []FMT: unknown phrase in the format");
    }
    if (p.kind == FM_G) {
      char q;
      if (*i >= len || (s[*i] != '\'' && s[*i] != '"'))
        rt_error("DOMAIN ERROR: []FMT: G needs quoted text");
      q = s[*i]; (*i)++;
      fm_quoted(s, i, len, q, p.txt, (int)sizeof(p.txt));
    } else {
      p.w = fm_int(s, i, len, &has);
      if (!has && (p.kind == FM_X || p.kind == FM_T)) p.w = (p.kind == FM_X) ? 1 : 1;
      if (*i < len && s[*i] == '.' && (p.kind == FM_F || p.kind == FM_E)) {
        (*i)++;
        p.d = fm_int(s, i, len, &has);
      }
    }
    {
      int k;
      for (k = 0; k < rep; k++) fm_add(sp, &p);
    }
  }
}

/* ---- one number into a field ---- */
static void fm_commas(char *digits)          /* "1234567" -> "1,234,567" */
{
  char tmp[128];
  int n = (int)strlen(digits), i, k = 0;
  if (n > 40) return;
  for (i = 0; i < n; i++) {
    if (i && (n - i) % 3 == 0) tmp[k++] = ',';
    tmp[k++] = digits[i];
  }
  tmp[k] = '\0';
  strcpy(digits, tmp);
}

/* returns a new string of exactly w characters (w == 0: natural width) */
static char *fm_number(const FmPh *p, double v)
{
  char body[320], num[160], *res, *pre = NULL, *post = NULL;
  int neg, w = p->w, len, fill;
  const char *sg = "";
  double sc = v;
  int k;
  if (p->kq > 0) for (k = 0; k < p->kq; k++) sc *= 10.0;
  if (p->kq < 0) for (k = 0; k < -p->kq; k++) sc /= 10.0;
  if (fabs(sc) > 1e30) {
    int sw = w > 0 ? w : 1;
    res = (char *)xmalloc((size_t)sw + 1);
    memset(res, '*', (size_t)sw);
    res[sw] = '\0';
    return res;
  }
  if (p->kind == FM_I) sc = floor(fabs(sc) + 0.5) * (sc < 0 ? -1.0 : 1.0);
  neg = sc < 0.0;
  if (p->bq && sc == 0.0) { num[0] = '\0'; goto place; }
  if (p->kind == FM_I) sprintf(num, "%.0f", fabs(sc));
  else if (p->kind == FM_F) {
    sprintf(num, "%.*f", p->d, fabs(sc));
    if (strtod(num, NULL) == 0.0) neg = 0;
  } else {                                   /* E: d significant digits */
    int dg = p->d > 0 ? p->d - 1 : 0;
    char *e;
    sprintf(num, "%.*E", dg, fabs(sc));
    e = strchr(num, 'E');
    if (e) {                                  /* 1.23E+04 -> 1.23E4 / 1.23E-4 */
      int ex = atoi(e + 1);
      sprintf(e, "E%d", ex);
    }
  }
  if (p->cq && p->kind != FM_E) {
    char *dot = strchr(num, '.');
    if (dot) {
      char frac[96];
      strcpy(frac, dot);
      *dot = '\0';
      fm_commas(num);
      strcat(num, frac);
    } else fm_commas(num);
  }
place:
  if (neg) {
    if (p->hasm) post = (char *)p->mtxt;
    else if (p->hasn) pre = (char *)p->ntxt;
    else sg = "-";
  }
  strcpy(body, "");
  if (pre) strcat(body, pre);
  strcat(body, sg);
  strcat(body, num);
  if (post) strcat(body, post);
  else if (p->hasm && !neg) { int q = (int)strlen(p->mtxt); while (q--) strcat(body, " "); }
  if (p->bq && sc == 0.0) body[0] = '\0';
  len = (int)strlen(body);
  if (w == 0) w = len;
  if (len > w && p->kind == FM_F && body[0] == '0' && body[1] == '.' && len - 1 <= w) {
    memmove(body, body + 1, (size_t)len);       /* drop the leading zero */
    len--;
  } else if (len > w && p->kind == FM_F && body[0] == '-' && body[1] == '0' &&
             body[2] == '.' && len - 1 <= w) {
    memmove(body + 1, body + 2, (size_t)len - 1);
    len--;
  }
  res = (char *)xmalloc((size_t)w + 1);
  if (len > w) { memset(res, '*', (size_t)w); res[w] = '\0'; return res; }
  fill = w - len;
  if (p->lq) {
    memcpy(res, body, (size_t)len);
    memset(res + len, ' ', (size_t)fill);
  } else if (p->zq && !p->bq) {
    int signlen = (body[0] == '-') ? 1 : 0;
    memcpy(res, body, (size_t)signlen);
    memset(res + signlen, '0', (size_t)fill);
    memcpy(res + signlen + fill, body + signlen, (size_t)(len - signlen));
  } else {
    memset(res, ' ', (size_t)fill);
    memcpy(res + fill, body, (size_t)len);
  }
  res[w] = '\0';
  return res;
}

/* G: each 9 in the pattern takes a digit of the rounded value, from the right */
static char *fm_pattern(const FmPh *p, double v)
{
  char digits[64];
  int n9 = 0, i, di, len = (int)strlen(p->txt), nd;
  char *res = (char *)xmalloc((size_t)len + 1);
  double sc = v;
  int k;
  for (k = 0; k < p->kq; k++) sc *= 10.0;
  for (k = 0; k < -p->kq; k++) sc /= 10.0;
  sc = floor(fabs(sc) + 0.5);
  if (sc >= 1e15) sc = 0.0;
  sprintf(digits, "%.0f", sc);
  nd = (int)strlen(digits);
  for (i = 0; i < len; i++) if (p->txt[i] == '9') n9++;
  di = nd - 1;
  res[len] = '\0';
  for (i = len - 1; i >= 0; i--) {
    if (p->txt[i] == '9') {
      if (di >= 0) res[i] = digits[di--];
      else res[i] = p->zq ? '0' : ' ';
    } else res[i] = p->txt[i];
  }
  if (di >= 0) memset(res, '*', (size_t)len);       /* did not fit */
  return res;
}

/* ---- the blocks of data ---- */
typedef struct FmBlk {
  const Arr *a;
  int ischar, rows, cols;
} FmBlk;

static void fm_dims(const Arr *a, FmBlk *b)
{
  b->a = a;
  b->ischar = (a->type == T_CHR);
  if (a->type == T_NEST || a->type == T_CPX)
    rt_error("DOMAIN ERROR: []FMT: the data must be numbers or text");
  if (a->rank == 0) { b->rows = 1; b->cols = 1; }
  else if (a->rank == 1) {
    if (b->ischar) { b->rows = 1; b->cols = a->n; }
    else { b->rows = a->n; b->cols = 1; }
  } else if (a->rank == 2) { b->rows = a->shape[0]; b->cols = a->shape[1]; }
  else rt_error("RANK ERROR: []FMT: the data has too many axes");
}

typedef struct FmLine { char *s; int len, cap; } FmLine;
static void fl_put(FmLine *l, int pos, const char *t, int n)
{
  if (pos + n > l->cap) {
    int nc = pos + n + 64;
    l->s = (char *)xrealloc(l->s, (size_t)nc);
    memset(l->s + l->cap, ' ', (size_t)(nc - l->cap));
    l->cap = nc;
  }
  memcpy(l->s + pos, t, (size_t)n);
  if (pos + n > l->len) l->len = pos + n;
}

static Arr *fmt_dyadic(Arr *spec, Arr *data)
{
  FmSpec *spp;
  FmBlk *bl;
  int nbl = 0, i, rows = 0, r, nsp, specrows;
  int len;
  FmLine *lines;
  Arr *res;
  int maxw = 0;
  char *sbuf;
  int pos0 = 0;

  if (spec->type != T_CHR || spec->rank > 1)
    rt_error("DOMAIN ERROR: []FMT: the format must be a text vector");
  len = spec->n;
  sbuf = xstrndup(spec->chr, len);
  trk_add(sbuf, 0);
  spp = (FmSpec *)xmalloc(sizeof(FmSpec));
  spp->n = 0;
  trk_add(spp, 0);
  fm_parse(sbuf, &pos0, len, spp, 0);
  trk_del(sbuf);
  free(sbuf);
  nsp = spp->n;

  /* blocks */
  if (data->type == T_NEST && data->rank <= 1 && !(data->rank == 0)) {
    nbl = data->n;
    bl = (FmBlk *)xmalloc(sizeof(FmBlk) * (size_t)(nbl ? nbl : 1));
    trk_add(bl, 0);
    for (i = 0; i < nbl; i++) fm_dims(data->sub[i], &bl[i]);
  } else {
    nbl = 1;
    bl = (FmBlk *)xmalloc(sizeof(FmBlk));
    trk_add(bl, 0);
    fm_dims(data, &bl[0]);
  }
  for (i = 0; i < nbl; i++) {
    if (bl[i].rows == 1 && !bl[i].ischar && bl[i].cols == 1 && bl[i].a->rank == 0)
      continue;                              /* a scalar extends */
    if (rows == 0) rows = bl[i].rows;
    else if (bl[i].rows != rows && bl[i].rows != 1)
      rt_error("LENGTH ERROR: []FMT: the columns have different lengths");
  }
  if (rows == 0) {
    rows = 1;
    for (i = 0; i < nbl; i++) if (bl[i].rows > rows) rows = bl[i].rows;
  }
  specrows = rows;

  lines = (FmLine *)xmalloc(sizeof(FmLine) * (size_t)(rows ? rows : 1));
  trk_add(lines, 0);
  for (r = 0; r < rows; r++) { lines[r].s = NULL; lines[r].len = 0; lines[r].cap = 0; }

  {
    int pi = 0;                 /* phrase index (cyclic) */
    int bi = 0, col = 0;        /* current block and column inside it */
    int ndata = 0, guard = 0;
    int *cur = (int *)xmalloc(sizeof(int) * (size_t)(rows ? rows : 1));
    trk_add(cur, 0);
    for (i = 0; i < nsp; i++)
      if (spp->ph[i].kind != FM_X && spp->ph[i].kind != FM_T && spp->ph[i].kind != FM_TEXT)
        ndata++;
    /* the column position of each line (for T) */
    for (r = 0; r < rows; r++) cur[r] = 0;
    if (nbl > 0 && ndata == 0 && nsp > 0) {
      /* text only: print it for every row */
    }
    while (bi < nbl || (nsp > 0 && ndata == 0 && guard == 0)) {
      const FmPh *p;
      if (nsp == 0) break;
      p = &spp->ph[pi % nsp];
      guard++;
      if (guard > 100000) break;
      if (p->kind == FM_TEXT || p->kind == FM_X || p->kind == FM_T) {
        for (r = 0; r < rows; r++) {
          if (p->kind == FM_TEXT) {
            fl_put(&lines[r], cur[r], p->txt, (int)strlen(p->txt));
            cur[r] += (int)strlen(p->txt);
          } else if (p->kind == FM_X) {
            char *bk = (char *)xmalloc((size_t)p->w + 1);
            memset(bk, ' ', (size_t)p->w);
            fl_put(&lines[r], cur[r], bk, p->w);
            free(bk);
            cur[r] += p->w;
          } else {
            cur[r] = p->w > 0 ? p->w - 1 : 0;
            if (cur[r] > lines[r].len) {
              char *bk = (char *)xmalloc((size_t)(cur[r] - lines[r].len) + 1);
              memset(bk, ' ', (size_t)(cur[r] - lines[r].len));
              fl_put(&lines[r], lines[r].len, bk, cur[r] - lines[r].len);
              free(bk);
            }
          }
        }
        pi++;
        if (ndata == 0) break;
        continue;
      }
      /* a data phrase */
      if (bi >= nbl) break;
      {
        const FmBlk *b = &bl[bi];
        if (b->ischar != (p->kind == FM_A)) {
          const char *msg = b->ischar ? "DOMAIN ERROR: []FMT: text needs an A phrase"
                                      : "DOMAIN ERROR: []FMT: numbers need I, F, E or G";
          for (r = 0; r < rows; r++) free(lines[r].s);
          trk_del(cur); free(cur);
          trk_del(lines); free(lines);
          rt_error(msg);
        }
        for (r = 0; r < rows; r++) {
          char *fld;
          int w;
          int rr = (b->rows == 1) ? 0 : r;
          if (b->ischar) {
            int cc = b->cols, k;
            w = p->w > 0 ? p->w : cc;
            fld = (char *)xmalloc((size_t)w + 1);
            memset(fld, ' ', (size_t)w);
            for (k = 0; k < cc && k < w; k++)
              fld[k] = (b->a->rank == 0) ? b->a->chr[0]
                       : b->a->chr[rr * b->cols + k];
            fld[w] = '\0';
          } else {
            double v = b->a->num[b->a->rank == 0 ? 0 : rr * b->cols + col];
            if (p->kind == FM_G) fld = fm_pattern(p, v);
            else fld = fm_number(p, v);
            w = (int)strlen(fld);
          }
          fl_put(&lines[r], cur[r], fld, w);
          cur[r] += w;
          free(fld);
        }
        if (b->ischar) { bi++; col = 0; }
        else { col++; if (col >= b->cols) { bi++; col = 0; } }
        pi++;
        if (bi >= nbl) {
          /* trailing text and blanks that follow the last field */
          while (pi % (nsp ? nsp : 1) != 0 || pi == 0) {
            const FmPh *q;
            if (nsp == 0) break;
            q = &spp->ph[pi % nsp];
            if (q->kind != FM_TEXT && q->kind != FM_X) break;
            for (r = 0; r < rows; r++) {
              if (q->kind == FM_TEXT) {
                fl_put(&lines[r], cur[r], q->txt, (int)strlen(q->txt));
                cur[r] += (int)strlen(q->txt);
              } else {
                char *bk = (char *)xmalloc((size_t)q->w + 1);
                memset(bk, ' ', (size_t)q->w);
                fl_put(&lines[r], cur[r], bk, q->w);
                free(bk);
                cur[r] += q->w;
              }
            }
            pi++;
          }
        }
      }
    }
    trk_del(cur);
    free(cur);
  }
  trk_del(spp); free(spp);
  trk_del(bl); free(bl);
  for (r = 0; r < rows; r++) if (lines[r].len > maxw) maxw = lines[r].len;
  {
    int sh[2];
    sh[0] = rows; sh[1] = maxw;
    res = a_new(T_CHR, 2, sh);
  }
  for (r = 0; r < rows; r++) {
    memset(res->chr + r * maxw, ' ', (size_t)maxw);
    if (lines[r].len) memcpy(res->chr + r * maxw, lines[r].s, (size_t)lines[r].len);
    free(lines[r].s);
  }
  trk_del(lines);
  free(lines);
  (void)specrows;
  return res;
}

/* monadic: the display of x as a character matrix */
static Arr *fmt_monadic(Arr *x)
{
  Blk *b = render(x);
  Arr *res;
  int sh[2], i, n;
  sh[0] = b->h > 0 ? b->h : 1;
  sh[1] = b->w;
  res = a_new(T_CHR, 2, sh);
  memset(res->chr, ' ', (size_t)(sh[0] * sh[1]));
  for (i = 0; i < b->h; i++) {
    n = (int)strlen(b->ln[i]);
    if (n > b->w) n = b->w;
    memcpy(res->chr + i * sh[1], b->ln[i], (size_t)n);
  }
  blk_free(b);
  return res;
}
