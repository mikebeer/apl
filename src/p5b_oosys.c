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
