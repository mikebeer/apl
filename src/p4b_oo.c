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
