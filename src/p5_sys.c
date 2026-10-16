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
static void emit_numtxt(const Arr *a, int i, char *buf)
{
  if (a->type == T_CPX && a->im[i] != 0.0)
    sprintf(buf, "%.15gJ%.15g", a->num[i], a->im[i]);
  else sprintf(buf, "%.15g", a->num[i]);
}
static void emit_simple(FILE *fp, const Arr *a)
{
  int i;
  char buf[96];
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
  if (a->rank == 0) { emit_numtxt(a, 0, buf); fprintf(fp, "%s", buf); return; }
  if (a->rank > 1 || a->n <= 1) {
    fprintf(fp, "(");
    for (i = 0; i < a->rank; i++)
      fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
    fprintf(fp, ") rho ");
    if (a->n == 0) { fprintf(fp, "0"); return; }
    if (a->n == 1) { emit_numtxt(a, 0, buf); fprintf(fp, "%s", buf); return; }
  }
  fprintf(fp, "(");
  for (i = 0; i < a->n; i++) {
    emit_numtxt(a, i, buf);
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
    emit_numtxt(a, 0, buf);
    fprintf(fp, "%s\n", buf);
    return;
  }
  if (a->rank > 1 || a->n <= 1) {
    fprintf(fp, "(");
    for (i = 0; i < a->rank; i++) fprintf(fp, "%s%d", i ? " " : "", a->shape[i]);
    fprintf(fp, ") rho ");
    if (a->n == 0) { fprintf(fp, "0\n"); return; }
    if (a->n == 1) { emit_numtxt(a, 0, buf); fprintf(fp, "%s\n", buf); return; }
  }
  fprintf(fp, "(");
  for (i = 0; i < a->n; i++) {
    emit_numtxt(a, i, buf);
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
