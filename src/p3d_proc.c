/* p3d_proc.c - auxiliary processors with real code
 *
 *   AP 100  FILES      text and binary files, directory listing
 *   AP 124  SCREEN     full-screen output and keyboard (ANSI terminal)
 *   AP 160  TCP        TCP/IP client and server sockets
 *   AP 210  COMPFILE   component files: numbered APL values in a file
 *
 * All four use the same protocol.  The processor's variable is assigned a
 * REQUEST: a command text, or a vector whose first item is the command text
 * and whose other items are the arguments.  The processor carries out the
 * request when the assignment is made, and the variable then holds the
 * RESULT, which is read like any variable:
 *
 *      100 []SVO 'F'
 *      F <- 'open' 'notes.txt' 'w'
 *      h <- F                   # the handle
 *      F <- 'writeline' h 'hello'
 *
 * A request that cannot be carried out stops with an error (FILE ERROR,
 * NET ERROR, ...) that []EA can catch.  The same protocol is used by the
 * processors you add in ap_init_extra().
 */

/* ---- parsing a request ---- */
typedef struct Rq {
  Arr *a;                       /* the request as assigned */
  int n;                        /* number of arguments (after the command) */
  char cmd[32];                 /* the command, lower case */
} Rq;

static void rq_parse(Arr *val, Rq *q)
{
  const Arr *c;
  int i, len;
  q->a = val;
  q->n = 0;
  if (val->type == T_CHR && val->rank <= 1) {
    c = val;
  } else if (val->type == T_NEST && val->rank == 1 && val->n >= 1 &&
             val->sub[0]->type == T_CHR && val->sub[0]->rank <= 1) {
    c = val->sub[0];
    q->n = val->n - 1;
  } else {
    rt_error("DOMAIN ERROR: a request is a command text, or a vector that "
             "starts with one");
    return;
  }
  len = c->rank == 0 ? 1 : c->n;
  if (len >= (int)sizeof(q->cmd)) rt_error("DOMAIN ERROR: unknown command");
  for (i = 0; i < len; i++) {
    int ch = (unsigned char)c->chr[i];
    const char *p = strchr(UPPER, ch);
    q->cmd[i] = p && ch ? LOWER[p - UPPER] : (char)ch;
  }
  q->cmd[len] = '\0';
}
static Arr *rq_item(const Rq *q, int i)       /* i = 1.. : borrowed */
{
  if (i > q->n) rt_error("DOMAIN ERROR: argument missing for the command");
  return q->a->sub[i];
}
/* copy argument i, a text, into buf */
static void rq_str(const Rq *q, int i, char *buf, int max)
{
  Arr *a = rq_item(q, i);
  int n;
  if (a->type != T_CHR || a->rank > 1)
    rt_error("DOMAIN ERROR: the argument must be text");
  n = a->rank == 0 ? 1 : a->n;
  if (n >= max) rt_error("LIMIT ERROR: the text argument is too long");
  memcpy(buf, a->chr, (size_t)n);
  buf[n] = '\0';
}
static double rq_num(const Rq *q, int i)
{
  Arr *a = rq_item(q, i);
  if (a->type != T_NUM || a->n != 1)
    rt_error("DOMAIN ERROR: the argument must be a number");
  return a->num[0];
}
static int rq_int(const Rq *q, int i)
{
  double v = rq_num(q, i);
  if (v != floor(v) || fabs(v) > 2000000000.0)
    rt_error("DOMAIN ERROR: the argument must be a whole number");
  return (int)v;
}
static int rq_has(const Rq *q, int i) { return i <= q->n; }

/* results */
static Arr *res_text(const char *s, int n)
{
  Arr *r = a_vec(T_CHR, n);
  if (n > 0) memcpy(r->chr, s, (size_t)n);
  return r;
}
static Arr *res_none(void) { return a_vec(T_NUM, 0); }   /* zilch */

/* a char matrix of rows (padded) */
static Arr *res_rows(char **row, int nrow)
{
  int w = 0, i, sh[2];
  Arr *r;
  for (i = 0; i < nrow; i++) if ((int)strlen(row[i]) > w) w = (int)strlen(row[i]);
  sh[0] = nrow; sh[1] = w;
  r = a_new(T_CHR, 2, sh);
  memset(r->chr, ' ', (size_t)(nrow * w));
  for (i = 0; i < nrow; i++) memcpy(r->chr + i * w, row[i], strlen(row[i]));
  return r;
}

static void err_sys(const char *kind, const char *what, const char *name)
{
  char buf[200];
  sprintf(buf, "%.20s: %.60s %.60s (%.40s)", kind, what, name, strerror(errno));
  rt_error(buf);
}

/* ======================================================================
 * AP 100: FILES
 * ====================================================================== */
#define NFILES 32
typedef struct FileSlot { FILE *fp; char name[256]; int eof; } FileSlot;
static FileSlot ftab[NFILES];

static FileSlot *file_of(int h)
{
  if (h < 1 || h > NFILES || !ftab[h - 1].fp)
    rt_error("FILE ERROR: not an open file handle");
  return &ftab[h - 1];
}

static int wild_match(const char *pat, const char *s)
{
  if (*pat == '\0') return *s == '\0';
  if (*pat == '*') {
    do { if (wild_match(pat + 1, s)) return 1; } while (*s++);
    return 0;
  }
  if (*s == '\0') return 0;
  if (*pat == '?' || *pat == *s) return wild_match(pat + 1, s + 1);
  return 0;
}

static int cmp_str(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

#if defined(APL_HAVE_DIRENT)
#include <dirent.h>
#endif

static Arr *files_dir(const char *pat)
{
#if defined(APL_HAVE_DIRENT)
  DIR *d = opendir(".");
  struct dirent *e;
  char **names = NULL;
  int n = 0, i;
  Arr *r;
  if (!d) err_sys("FILE ERROR", "cannot read the directory", ".");
  while ((e = readdir(d)) != NULL) {
    if (!wild_match(pat, e->d_name)) continue;
    names = (char **)xrealloc(names, sizeof(char *) * (size_t)(n + 1));
    names[n++] = xstrdup(e->d_name);
  }
  closedir(d);
  if (n > 1) qsort(names, (size_t)n, sizeof(char *), cmp_str);
  r = n ? res_rows(names, n) : res_none();
  for (i = 0; i < n; i++) free(names[i]);
  free(names);
  return r;
#else
  (void)pat;
  rt_error("FILE ERROR: directory listing is not available on this platform");
  return NULL;
#endif
}

static long file_size_of(FILE *fp)
{
  long cur = ftell(fp), end;
  fseek(fp, 0L, SEEK_END);
  end = ftell(fp);
  fseek(fp, cur, SEEK_SET);
  return end;
}

static Arr *files_request(Arr *val)
{
  Rq q;
  char name[256], name2[256], mode[8];
  rq_parse(val, &q);
  if (strcmp(q.cmd, "open") == 0) {
    int h, rd = 0;
    const char *m;
    rq_str(&q, 1, name, sizeof name);
    strcpy(mode, "r");
    if (rq_has(&q, 2)) rq_str(&q, 2, mode, sizeof mode);
    if (strcmp(mode, "r") == 0) m = "rb";
    else if (strcmp(mode, "w") == 0) m = "wb";
    else if (strcmp(mode, "a") == 0) m = "ab";
    else if (strcmp(mode, "r+") == 0) { m = "r+b"; rd = 1; }
    else if (strcmp(mode, "w+") == 0) { m = "w+b"; rd = 1; }
    else if (strcmp(mode, "a+") == 0) { m = "a+b"; rd = 1; }
    else { (void)rd; rt_error("FILE ERROR: mode must be r, w, a, r+, w+ or a+"); return NULL; }
    for (h = 0; h < NFILES; h++) if (!ftab[h].fp) break;
    if (h == NFILES) rt_error("FILE ERROR: too many open files");
    ftab[h].fp = fopen(name, m);
    if (!ftab[h].fp) err_sys("FILE ERROR", "cannot open", name);
    strcpy(ftab[h].name, name);
    ftab[h].eof = 0;
    return a_scalar((double)(h + 1));
  }
  if (strcmp(q.cmd, "close") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    fclose(f->fp);
    f->fp = NULL;
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "readline") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    int c, n = 0, cap = 128;
    char *buf = (char *)xmalloc((size_t)cap);
    Arr *r;
    while ((c = getc(f->fp)) != EOF && c != '\n') {
      if (n == cap) { cap *= 2; buf = (char *)xrealloc(buf, (size_t)cap); }
      buf[n++] = (char)c;
    }
    if (c == EOF && n == 0) { free(buf); f->eof = 1; return res_none(); }
    if (c == EOF) f->eof = 1;
    if (n > 0 && buf[n - 1] == '\r') n--;
    r = res_text(buf, n);
    free(buf);
    return r;
  }
  if (strcmp(q.cmd, "read") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    int want = rq_int(&q, 2), got;
    char *buf;
    Arr *r;
    if (want < 0 || want > 16777216) rt_error("DOMAIN ERROR: bad byte count");
    buf = (char *)xmalloc((size_t)want + 1);
    got = (int)fread(buf, 1, (size_t)want, f->fp);
    if (got < want) f->eof = 1;
    if (got == 0 && want > 0) { free(buf); return res_none(); }
    r = res_text(buf, got);
    free(buf);
    return r;
  }
  if (strcmp(q.cmd, "write") == 0 || strcmp(q.cmd, "writeline") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    Arr *t = rq_item(&q, 2);
    int n;
    if (t->type != T_CHR || t->rank > 1) rt_error("DOMAIN ERROR: write needs text");
    n = t->rank == 0 ? 1 : t->n;
    if (n > 0 && fwrite(t->chr, 1, (size_t)n, f->fp) != (size_t)n)
      err_sys("FILE ERROR", "write failed on", f->name);
    if (q.cmd[5] == 'l') { putc('\n', f->fp); n++; }
    return a_scalar((double)n);
  }
  if (strcmp(q.cmd, "flush") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    fflush(f->fp);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "seek") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    long pos = (long)rq_num(&q, 2);
    int wh = rq_has(&q, 3) ? rq_int(&q, 3) : 0;
    if (wh < 0 || wh > 2) rt_error("DOMAIN ERROR: whence must be 0, 1 or 2");
    if (fseek(f->fp, pos, wh == 0 ? SEEK_SET : (wh == 1 ? SEEK_CUR : SEEK_END)) != 0)
      err_sys("FILE ERROR", "cannot seek in", f->name);
    f->eof = 0;
    return a_scalar((double)ftell(f->fp));
  }
  if (strcmp(q.cmd, "tell") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    return a_scalar((double)ftell(f->fp));
  }
  if (strcmp(q.cmd, "eof") == 0) {
    FileSlot *f = file_of(rq_int(&q, 1));
    int c;
    if (!f->eof) {
      c = getc(f->fp);
      if (c == EOF) f->eof = 1; else ungetc(c, f->fp);
    }
    return a_scalar(f->eof ? 1.0 : 0.0);
  }
  if (strcmp(q.cmd, "size") == 0) {
    Arr *a = rq_item(&q, 1);
    if (a->type == T_NUM) {
      FileSlot *f = file_of(rq_int(&q, 1));
      fflush(f->fp);
      return a_scalar((double)file_size_of(f->fp));
    } else {
      FILE *fp;
      long sz;
      rq_str(&q, 1, name, sizeof name);
      fp = fopen(name, "rb");
      if (!fp) err_sys("FILE ERROR", "cannot open", name);
      sz = file_size_of(fp);
      fclose(fp);
      return a_scalar((double)sz);
    }
  }
  if (strcmp(q.cmd, "exists") == 0) {
    FILE *fp;
    rq_str(&q, 1, name, sizeof name);
    fp = fopen(name, "rb");
    if (fp) fclose(fp);
    return a_scalar(fp ? 1.0 : 0.0);
  }
  if (strcmp(q.cmd, "delete") == 0) {
    rq_str(&q, 1, name, sizeof name);
    if (remove(name) != 0) err_sys("FILE ERROR", "cannot delete", name);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "rename") == 0) {
    rq_str(&q, 1, name, sizeof name);
    rq_str(&q, 2, name2, sizeof name2);
    if (rename(name, name2) != 0) err_sys("FILE ERROR", "cannot rename", name);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "readall") == 0 || strcmp(q.cmd, "lines") == 0) {
    FILE *fp;
    long sz;
    char *buf;
    Arr *r;
    int n;
    rq_str(&q, 1, name, sizeof name);
    fp = fopen(name, "rb");
    if (!fp) err_sys("FILE ERROR", "cannot open", name);
    sz = file_size_of(fp);
    if (sz > 16777216L) { fclose(fp); rt_error("FILE ERROR: file too large"); }
    buf = (char *)xmalloc((size_t)sz + 1);
    n = (int)fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (q.cmd[0] == 'r') {
      r = res_text(buf, n);
    } else {
      char **row = NULL;
      int nr = 0, i, st = 0;
      for (i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
          int e = i;
          if (i == n && st == n) break;
          if (e > st && buf[e - 1] == '\r') e--;
          row = (char **)xrealloc(row, sizeof(char *) * (size_t)(nr + 1));
          row[nr++] = xstrndup(buf + st, e - st);
          st = i + 1;
        }
      }
      r = nr ? res_rows(row, nr) : res_none();
      for (i = 0; i < nr; i++) free(row[i]);
      free(row);
    }
    free(buf);
    return r;
  }
  if (strcmp(q.cmd, "dir") == 0) {
    strcpy(name, "*");
    if (rq_has(&q, 1)) rq_str(&q, 1, name, sizeof name);
    return files_dir(name);
  }
  if (strcmp(q.cmd, "handles") == 0) {
    int i, n = 0;
    Arr *r;
    for (i = 0; i < NFILES; i++) if (ftab[i].fp) n++;
    r = a_vec(T_NUM, n);
    n = 0;
    for (i = 0; i < NFILES; i++) if (ftab[i].fp) r->num[n++] = (double)(i + 1);
    return r;
  }
  rt_error_s("DOMAIN ERROR: FILES has no command ", q.cmd, "");
  return NULL;
}

static Arr *files_set(const char *var, Arr *val)
{
  (void)var;
  return files_request(val);
}

/* ======================================================================
 * AP 210: COMPFILE - component files
 *
 * A component file holds numbered APL values (any simple or nested array).
 * It is a text file, so it moves between machines unchanged:
 *     APLCF1
 *     <length> <serialised value>      one line per component
 * ====================================================================== */
#define NCFILES 16
typedef struct CompFile {
  int used;
  char name[256];
  int n;                        /* number of components */
  char **item;                  /* serialised components */
} CompFile;
static CompFile cftab[NCFILES];

typedef struct Sb { char *s; int len, cap; } Sb;
static void sb_put(Sb *b, const char *t, int n)
{
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2 + 64;
    b->s = (char *)xrealloc(b->s, (size_t)b->cap);
  }
  memcpy(b->s + b->len, t, (size_t)n);
  b->len += n;
  b->s[b->len] = '\0';
}
static void sb_str(Sb *b, const char *t) { sb_put(b, t, (int)strlen(t)); }

/* returns 0 if a value cannot be stored (an object reference) */
static int ser_arr(Sb *b, const Arr *a)
{
  char buf[64];
  int i;
  if (a->obj || (a->type == T_NEST && a->rank == 0 && a->sub[0] && a->sub[0]->obj))
    return 0;
  sprintf(buf, "%c%d", a->type == T_NUM ? 'N' : a->type == T_CHR ? 'C' :
          a->type == T_CPX ? 'X' : 'E', a->rank);
  sb_str(b, buf);
  for (i = 0; i < a->rank; i++) { sprintf(buf, " %d", a->shape[i]); sb_str(b, buf); }
  sb_str(b, "\n");
  if (a->type == T_NUM) {
    for (i = 0; i < a->n; i++) { sprintf(buf, "%.17g ", a->num[i]); sb_str(b, buf); }
  } else if (a->type == T_CPX) {
    for (i = 0; i < a->n; i++) {
      sprintf(buf, "%.17g %.17g ", a->num[i], a->im[i]);
      sb_str(b, buf);
    }
  } else if (a->type == T_CHR) {
    for (i = 0; i < a->n; i++) { sprintf(buf, "%d ", (int)(unsigned char)a->chr[i]); sb_str(b, buf); }
  } else {
    for (i = 0; i < a->n; i++) if (!ser_arr(b, a->sub[i])) return 0;
  }
  sb_str(b, "\n");
  return 1;
}

/* the cursor into serialised text */
static Arr *deser_arr(const char **p)
{
  char *end;
  int type, rank, i, sh[MAXRANK];
  Arr *a;
  const char *q = *p;
  while (*q == ' ' || *q == '\n') q++;
  type = *q++;
  if (type != 'N' && type != 'C' && type != 'X' && type != 'E')
    rt_error("FILE ERROR: the component is damaged");
  rank = (int)strtol(q, &end, 10);
  if (end == q || rank < 0 || rank > MAXRANK) rt_error("FILE ERROR: the component is damaged");
  q = end;
  for (i = 0; i < rank; i++) {
    sh[i] = (int)strtol(q, &end, 10);
    if (end == q || sh[i] < 0) rt_error("FILE ERROR: the component is damaged");
    q = end;
  }
  a = a_new(type == 'N' ? T_NUM : type == 'C' ? T_CHR : type == 'X' ? T_CPX : T_NEST,
            rank, sh);
  for (i = 0; i < a->n; i++) {
    if (type == 'N') { a->num[i] = strtod(q, &end); q = end; }
    else if (type == 'X') {
      a->num[i] = strtod(q, &end); q = end;
      a->im[i] = strtod(q, &end); q = end;
    } else if (type == 'C') { a->chr[i] = (char)strtol(q, &end, 10); q = end; }
    else { a_put(a, i, deser_arr(&q)); }
  }
  *p = q;
  return a;
}

static CompFile *cf_of(int h)
{
  if (h < 1 || h > NCFILES || !cftab[h - 1].used)
    rt_error("FILE ERROR: not an open component file");
  return &cftab[h - 1];
}

static void cf_save(const CompFile *c)
{
  FILE *fp = fopen(c->name, "wb");
  int i;
  if (!fp) err_sys("FILE ERROR", "cannot write", c->name);
  fprintf(fp, "APLCF1\n");
  for (i = 0; i < c->n; i++) {
    fprintf(fp, "%d\n", (int)strlen(c->item[i]));
    fputs(c->item[i], fp);
    fputc('\n', fp);
  }
  fclose(fp);
}

static void cf_free_items(CompFile *c)
{
  int i;
  for (i = 0; i < c->n; i++) free(c->item[i]);
  free(c->item);
  c->item = NULL;
  c->n = 0;
}

/* load; the items go straight into c, so nothing leaks on an error */
static void cf_load(CompFile *c)
{
  FILE *fp = fopen(c->name, "rb");
  char line[64];
  int len;
  if (!fp) err_sys("FILE ERROR", "cannot open", c->name);
  if (!fgets(line, sizeof line, fp) || strncmp(line, "APLCF1", 6) != 0) {
    fclose(fp);
    rt_error("FILE ERROR: not a component file");
  }
  while (fgets(line, sizeof line, fp)) {
    char *s;
    len = atoi(line);
    if (len <= 0 || len > 100000000) { fclose(fp); rt_error("FILE ERROR: the component file is damaged"); }
    s = (char *)xmalloc((size_t)len + 2);
    if (fread(s, 1, (size_t)len + 1, fp) != (size_t)len + 1) {
      free(s); fclose(fp);
      rt_error("FILE ERROR: the component file is damaged");
    }
    s[len] = '\0';
    c->item = (char **)xrealloc(c->item, sizeof(char *) * (size_t)(c->n + 1));
    c->item[c->n++] = s;
  }
  fclose(fp);
}

static char *cf_serialise(const Arr *v)
{
  Sb b;
  b.s = NULL; b.len = 0; b.cap = 0;
  if (!ser_arr(&b, v)) { free(b.s); rt_error("DOMAIN ERROR: an object reference cannot be stored in a file"); }
  if (!b.s) sb_str(&b, "");
  return b.s;
}

static Arr *comp_request(Arr *val)
{
  Rq q;
  char name[256];
  rq_parse(val, &q);
  if (strcmp(q.cmd, "create") == 0 || strcmp(q.cmd, "open") == 0) {
    int h, create = (q.cmd[0] == 'c');
    FILE *fp;
    rq_str(&q, 1, name, sizeof name);
    for (h = 0; h < NCFILES; h++) if (!cftab[h].used) break;
    if (h == NCFILES) rt_error("FILE ERROR: too many open component files");
    fp = fopen(name, "rb");
    cf_free_items(&cftab[h]);              /* leftovers of a failed open */
    if (create) {
      if (fp) { fclose(fp); rt_error("FILE ERROR: the file already exists"); }
      strcpy(cftab[h].name, name);
      cf_save(&cftab[h]);
    } else {
      if (!fp) err_sys("FILE ERROR", "cannot open", name);
      fclose(fp);
      strcpy(cftab[h].name, name);
      cf_load(&cftab[h]);
    }
    cftab[h].used = 1;
    return a_scalar((double)(h + 1));
  }
  if (strcmp(q.cmd, "close") == 0) {
    CompFile *c = cf_of(rq_int(&q, 1));
    cf_free_items(c);
    c->used = 0;
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "count") == 0) return a_scalar((double)cf_of(rq_int(&q, 1))->n);
  if (strcmp(q.cmd, "append") == 0) {
    CompFile *c = cf_of(rq_int(&q, 1));
    char *s = cf_serialise(rq_item(&q, 2));
    FILE *fp;
    c->item = (char **)xrealloc(c->item, sizeof(char *) * (size_t)(c->n + 1));
    c->item[c->n++] = s;
    fp = fopen(c->name, "ab");               /* only the new component is written */
    if (!fp) err_sys("FILE ERROR", "cannot write", c->name);
    fprintf(fp, "%d\n", (int)strlen(s));
    fputs(s, fp);
    fputc('\n', fp);
    fclose(fp);
    return a_scalar((double)c->n);
  }
  if (strcmp(q.cmd, "read") == 0) {
    CompFile *c = cf_of(rq_int(&q, 1));
    int k = rq_int(&q, 2);
    const char *p;
    if (k < 1 || k > c->n) rt_error("INDEX ERROR: no such component");
    p = c->item[k - 1];
    return deser_arr(&p);
  }
  if (strcmp(q.cmd, "replace") == 0) {
    CompFile *c = cf_of(rq_int(&q, 1));
    int k = rq_int(&q, 2);
    char *s;
    if (k < 1 || k > c->n) rt_error("INDEX ERROR: no such component");
    s = cf_serialise(rq_item(&q, 3));
    free(c->item[k - 1]);
    c->item[k - 1] = s;
    cf_save(c);
    return a_scalar((double)k);
  }
  if (strcmp(q.cmd, "drop") == 0) {          /* remove the last component */
    CompFile *c = cf_of(rq_int(&q, 1));
    if (c->n == 0) rt_error("INDEX ERROR: the file has no components");
    free(c->item[--c->n]);
    cf_save(c);
    return a_scalar((double)c->n);
  }
  if (strcmp(q.cmd, "erase") == 0) {
    rq_str(&q, 1, name, sizeof name);
    if (remove(name) != 0) err_sys("FILE ERROR", "cannot delete", name);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "exists") == 0) {
    FILE *fp;
    rq_str(&q, 1, name, sizeof name);
    fp = fopen(name, "rb");
    if (fp) fclose(fp);
    return a_scalar(fp ? 1.0 : 0.0);
  }
  rt_error_s("DOMAIN ERROR: COMPFILE has no command ", q.cmd, "");
  return NULL;
}

static Arr *comp_set(const char *var, Arr *val)
{
  (void)var;
  return comp_request(val);
}

/* ======================================================================
 * AP 124: SCREEN - full-screen output and keyboard (ANSI / VT100 terminal)
 *
 *   'clear'                    erase the screen, cursor home
 *   'put' row col text         write text at row, col (1 is the top left)
 *   'cursor' row col           move the cursor
 *   'attr' n ...               set attributes: 0 normal 1 bold 4 underline
 *                              7 reverse 30-37 text colour 40-47 background
 *   'box' row col h w          draw a frame with + - |
 *   'size'                     rows, columns
 *   'getkey' [ms]              wait for a key: its code; 0 on time out,
 *                              -1 at end of input.  Special keys:
 *                              1001 up 1002 down 1003 right 1004 left
 *                              1005 home 1006 end 1007 delete 1008 page up
 *                              1009 page down; 13 enter, 8 backspace, 27 esc
 *   'input' row col len [text] edit a field; returns its text
 *   'bell'  'flush'  'hide'  'show'  (cursor)
 * ====================================================================== */
static int scr_raw = 0;
#ifdef APL_HAVE_TERMIOS
static struct termios scr_saved;
static void scr_raw_off(void)
{
  if (scr_raw) { tcsetattr(0, TCSANOW, &scr_saved); scr_raw = 0; }
}
static void scr_raw_on(void)
{
  struct termios t;
  if (scr_raw || !isatty(0)) return;
  if (tcgetattr(0, &scr_saved) != 0) return;
  t = scr_saved;
  t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(0, TCSANOW, &t) == 0) { scr_raw = 1; atexit(scr_raw_off); }
}
#else
static void scr_raw_off(void) {}
static void scr_raw_on(void) {}
#endif

/* one byte of input; ms < 0 waits for ever.  -2 on time out, -1 at the end */
static int scr_byte(int ms)
{
#ifdef APL_HAVE_SOCKETS
  if (ms >= 0) {
    fd_set fs;
    struct timeval tv;
    FD_ZERO(&fs);
    FD_SET(0, &fs);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    if (select(1, &fs, NULL, NULL, &tv) <= 0) return -2;
  }
#else
  (void)ms;
#endif
#ifdef APL_HAVE_TERMIOS
  if (scr_raw) {
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    return c;
  }
#endif
  {
    int c = getchar();
    return c == EOF ? -1 : c;
  }
}

static int scr_getkey(int ms)
{
  int c;
  scr_raw_on();
  c = scr_byte(ms);
  if (c == -2) return 0;
  if (c == -1) return -1;
  if (c == 10) return 13;
  if (c == 127) return 8;
  if (c == 27) {
    int d = scr_byte(60);
    int e;
    if (d < 0) return 27;
    if (d != '[' && d != 'O') return 27;
    e = scr_byte(60);
    switch (e) {
    case 'A': return 1001;
    case 'B': return 1002;
    case 'C': return 1003;
    case 'D': return 1004;
    case 'H': return 1005;
    case 'F': return 1006;
    case '3': scr_byte(60); return 1007;
    case '5': scr_byte(60); return 1008;
    case '6': scr_byte(60); return 1009;
    case '1': case '7': scr_byte(60); return 1005;
    case '4': case '8': scr_byte(60); return 1006;
    }
    return 27;
  }
  return c;
}

static void scr_goto(int r, int c) { printf("\033[%d;%dH", r, c); }

static Arr *scr_input(int r, int c, int len, const char *init)
{
  char buf[256];
  int n, key, i;
  if (len < 1) len = 1;
  if (len > 255) len = 255;
  strncpy(buf, init, (size_t)len);
  buf[len] = '\0';
  n = (int)strlen(buf);
  if (!isatty(0)) {                   /* plain input: read a line */
    char line[512];
    int l;
    scr_goto(r, c);
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return res_none();
    l = (int)strlen(line);
    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) l--;
    if (l > len) l = len;
    return res_text(line, l);
  }
  for (;;) {
    scr_goto(r, c);
    fwrite(buf, 1, (size_t)n, stdout);
    for (i = n; i < len; i++) putchar('_');
    scr_goto(r, c + n);
    fflush(stdout);
    key = scr_getkey(-1);
    if (key == 13 || key == -1) break;
    if (key == 27) { n = 0; buf[0] = '\0'; break; }
    if (key == 8) { if (n > 0) buf[--n] = '\0'; continue; }
    if (key >= 32 && key < 256 && n < len) { buf[n++] = (char)key; buf[n] = '\0'; }
  }
  return res_text(buf, n);
}

static Arr *screen_request(Arr *val)
{
  Rq q;
  rq_parse(val, &q);
  if (strcmp(q.cmd, "clear") == 0) { printf("\033[2J\033[H"); fflush(stdout); return a_scalar(1.0); }
  if (strcmp(q.cmd, "put") == 0) {
    Arr *t = rq_item(&q, 3);
    int n;
    if (t->type != T_CHR || t->rank > 1) rt_error("DOMAIN ERROR: put needs text");
    n = t->rank == 0 ? 1 : t->n;
    scr_goto(rq_int(&q, 1), rq_int(&q, 2));
    fwrite(t->chr, 1, (size_t)n, stdout);
    fflush(stdout);
    return a_scalar((double)n);
  }
  if (strcmp(q.cmd, "cursor") == 0) {
    scr_goto(rq_int(&q, 1), rq_int(&q, 2));
    fflush(stdout);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "attr") == 0) {
    int i;
    printf("\033[");
    if (q.n == 0) printf("0");
    for (i = 1; i <= q.n; i++) printf("%s%d", i > 1 ? ";" : "", rq_int(&q, i));
    printf("m");
    fflush(stdout);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "box") == 0) {
    int r = rq_int(&q, 1), c = rq_int(&q, 2), h = rq_int(&q, 3), w = rq_int(&q, 4), i, j;
    if (h < 2 || w < 2) rt_error("DOMAIN ERROR: a box is at least 2 by 2");
    for (i = 0; i < h; i++) {
      scr_goto(r + i, c);
      for (j = 0; j < w; j++) {
        int edge_r = (i == 0 || i == h - 1), edge_c = (j == 0 || j == w - 1);
        putchar(edge_r && edge_c ? '+' : edge_r ? '-' : edge_c ? '|' : ' ');
      }
    }
    fflush(stdout);
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "size") == 0) {
    Arr *r = a_vec(T_NUM, 2);
    const char *l = getenv("LINES"), *c = getenv("COLUMNS");
    r->num[0] = (l && atoi(l) > 0) ? atoi(l) : 24;
    r->num[1] = (c && atoi(c) > 0) ? atoi(c) : 80;
    return r;
  }
  if (strcmp(q.cmd, "getkey") == 0) {
    fflush(stdout);
    return a_scalar((double)scr_getkey(rq_has(&q, 1) ? rq_int(&q, 1) : -1));
  }
  if (strcmp(q.cmd, "input") == 0) {
    char init[256];
    init[0] = '\0';
    if (rq_has(&q, 4)) rq_str(&q, 4, init, sizeof init);
    return scr_input(rq_int(&q, 1), rq_int(&q, 2), rq_int(&q, 3), init);
  }
  if (strcmp(q.cmd, "bell") == 0) { putchar('\007'); fflush(stdout); return a_scalar(1.0); }
  if (strcmp(q.cmd, "flush") == 0) { fflush(stdout); return a_scalar(1.0); }
  if (strcmp(q.cmd, "hide") == 0) { printf("\033[?25l"); fflush(stdout); return a_scalar(1.0); }
  if (strcmp(q.cmd, "show") == 0) { printf("\033[?25h"); fflush(stdout); return a_scalar(1.0); }
  if (strcmp(q.cmd, "reset") == 0) {
    printf("\033[0m\033[?25h");
    fflush(stdout);
    scr_raw_off();
    return a_scalar(1.0);
  }
  rt_error_s("DOMAIN ERROR: SCREEN has no command ", q.cmd, "");
  return NULL;
}
static Arr *screen_set(const char *var, Arr *val)
{
  (void)var;
  return screen_request(val);
}

/* ======================================================================
 * AP 160: TCP - TCP/IP sockets
 *
 *   'connect' host port        open a connection; returns a handle
 *   'listen' port [host]       listen (port 0: any free port); a handle
 *   'accept' h [ms]            wait for a client: a new handle, 0 on time out
 *   'send' h text              send; the number of bytes
 *   'recv' h n [ms]            up to n bytes: text; zilch if there is nothing
 *                              (time out or the other side has closed)
 *   'recvline' h [ms]          one line without its line end; same rule
 *   'poll' h [ms]              1 if there is something to read or accept
 *   'status' h                 1 once the other side has closed and all
 *                              data has been read
 *   'port' h                   the local port    'peer' h  the other address
 *   'resolve' host             the numeric address of a host name
 *   'close' h                  'handles'  the open handles
 * Times are in milliseconds; no time means wait for ever.
 * ====================================================================== */
#define NNET 32
#ifdef APL_HAVE_SOCKETS
typedef struct NetSlot {
  int used, fd, listening, eof;
  char rbuf[4096];
  int rlen, rpos;
  char peer[96];
} NetSlot;
static NetSlot ntab[NNET];
static int net_sigpipe = 0;

static void net_err(const char *what, int code, int gai)
{
  char buf[200];
  sprintf(buf, "NET ERROR: %.60s (%.100s)", what,
          gai ? gai_strerror(code) : strerror(code));
  rt_error(buf);
}

static NetSlot *net_of(int h)
{
  if (h < 1 || h > NNET || !ntab[h - 1].used)
    rt_error("NET ERROR: not an open connection");
  return &ntab[h - 1];
}
static NetSlot *net_new(int fd, int *hp)
{
  int h;
  for (h = 0; h < NNET; h++) if (!ntab[h].used) break;
  if (h == NNET) { close(fd); rt_error("NET ERROR: too many open connections"); }
  memset(&ntab[h], 0, sizeof(NetSlot));
  ntab[h].used = 1;
  ntab[h].fd = fd;
  *hp = h + 1;
  return &ntab[h];
}
/* 1 when fd is readable within ms, 0 on time out */
static int net_wait(int fd, int ms)
{
  fd_set fs;
  struct timeval tv, *tp = NULL;
  int r;
  FD_ZERO(&fs);
  FD_SET(fd, &fs);
  if (ms >= 0) { tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000; tp = &tv; }
  r = select(fd + 1, &fs, NULL, NULL, tp);
  if (r < 0 && errno != EINTR) net_err("select failed", errno, 0);
  return r > 0;
}
/* fill the read buffer; returns the bytes now buffered (0: nothing/closed) */
static int net_fill(NetSlot *s, int ms)
{
  int n;
  if (s->rpos < s->rlen) return s->rlen - s->rpos;
  s->rpos = s->rlen = 0;
  if (s->eof) return 0;
  if (!net_wait(s->fd, ms)) return 0;
  n = (int)recv(s->fd, s->rbuf, sizeof s->rbuf, 0);
  if (n < 0) net_err("receive failed", errno, 0);
  if (n == 0) { s->eof = 1; return 0; }
  s->rlen = n;
  return n;
}
static void net_peer_text(NetSlot *s, int local)
{
  struct sockaddr_storage ss;
  socklen_t l = sizeof ss;
  char host[64];
  int port = 0;
  host[0] = '\0';
  if ((local ? getsockname(s->fd, (struct sockaddr *)&ss, &l)
             : getpeername(s->fd, (struct sockaddr *)&ss, &l)) != 0) return;
  if (ss.ss_family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)&ss;
    inet_ntop(AF_INET, &a->sin_addr, host, sizeof host);
    port = ntohs(a->sin_port);
  } else if (ss.ss_family == AF_INET6) {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
    inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof host);
    port = ntohs(a->sin6_port);
  }
  if (!local) sprintf(s->peer, "%.64s:%d", host, port);
  else sprintf(s->peer, "%d", port);
}

static Arr *tcp_request(Arr *val)
{
  Rq q;
  char host[256], portstr[16];
  rq_parse(val, &q);
  if (!net_sigpipe) {
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    net_sigpipe = 1;
  }
  if (strcmp(q.cmd, "connect") == 0 || strcmp(q.cmd, "listen") == 0) {
    struct addrinfo hints, *res, *ai;
    int fd = -1, rc, h, listening = (q.cmd[0] == 'l'), port, e = 0;
    NetSlot *s;
    if (listening) {
      port = rq_int(&q, 1);
      host[0] = '\0';
      if (rq_has(&q, 2)) rq_str(&q, 2, host, sizeof host);
    } else {
      rq_str(&q, 1, host, sizeof host);
      port = rq_int(&q, 2);
    }
    if (port < 0 || port > 65535) rt_error("DOMAIN ERROR: bad port number");
    sprintf(portstr, "%d", port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (listening) hints.ai_flags = AI_PASSIVE;
    rc = getaddrinfo(host[0] ? host : NULL, portstr, &hints, &res);
    if (rc != 0) net_err("cannot resolve the address", rc, 1);
    for (ai = res; ai; ai = ai->ai_next) {
      fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (fd < 0) { e = errno; continue; }
      if (listening) {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(fd, 16) == 0) break;
      } else if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
      e = errno;
      close(fd);
      fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) net_err(listening ? "cannot listen" : "cannot connect", e ? e : ECONNREFUSED, 0);
    s = net_new(fd, &h);
    s->listening = listening;
    net_peer_text(s, 0);
    return a_scalar((double)h);
  }
  if (strcmp(q.cmd, "accept") == 0) {
    NetSlot *l = net_of(rq_int(&q, 1)), *c;
    int fd, h;
    if (!l->listening) rt_error("NET ERROR: not a listening connection");
    if (!net_wait(l->fd, rq_has(&q, 2) ? rq_int(&q, 2) : -1)) return a_scalar(0.0);
    fd = accept(l->fd, NULL, NULL);
    if (fd < 0) net_err("accept failed", errno, 0);
    c = net_new(fd, &h);
    net_peer_text(c, 0);
    return a_scalar((double)h);
  }
  if (strcmp(q.cmd, "send") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    Arr *t = rq_item(&q, 2);
    int n, sent = 0;
    if (t->type != T_CHR || t->rank > 1) rt_error("DOMAIN ERROR: send needs text");
    n = t->rank == 0 ? 1 : t->n;
    while (sent < n) {
      int k = (int)send(s->fd, t->chr + sent, (size_t)(n - sent), 0);
      if (k < 0) {
        if (errno == EINTR) continue;
        net_err("send failed", errno, 0);
      }
      sent += k;
    }
    return a_scalar((double)sent);
  }
  if (strcmp(q.cmd, "recv") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    int want = rq_int(&q, 2), avail, k;
    Arr *r;
    if (want < 1) rt_error("DOMAIN ERROR: recv needs a positive count");
    avail = net_fill(s, rq_has(&q, 3) ? rq_int(&q, 3) : -1);
    if (avail == 0) return res_none();
    k = avail < want ? avail : want;
    r = res_text(s->rbuf + s->rpos, k);
    s->rpos += k;
    return r;
  }
  if (strcmp(q.cmd, "recvline") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    int ms = rq_has(&q, 2) ? rq_int(&q, 2) : -1;
    Sb b;
    Arr *r;
    int done = 0, any = 0;
    b.s = NULL; b.len = 0; b.cap = 0;
    while (!done) {
      int avail = net_fill(s, ms), i;
      if (avail == 0) break;
      for (i = s->rpos; i < s->rlen; i++) {
        if (s->rbuf[i] == '\n') { done = 1; i++; break; }
      }
      /* i is one past the newline, or the end of the buffer */
      sb_put(&b, s->rbuf + s->rpos, i - s->rpos);
      s->rpos = i;
      any = 1;
    }
    if (!done && !(any && s->eof)) {
      /* nothing complete: keep what we have for the next call */
      if (any) {
        int keep = b.len;
        if (keep > (int)sizeof s->rbuf) keep = (int)sizeof s->rbuf;
        memcpy(s->rbuf, b.s, (size_t)keep);
        s->rpos = 0; s->rlen = keep;
      }
      free(b.s);
      return res_none();
    }
    if (b.len > 0 && b.s[b.len - 1] == '\n') b.len--;
    if (b.len > 0 && b.s[b.len - 1] == '\r') b.len--;
    r = res_text(b.s ? b.s : "", b.len);
    free(b.s);
    return r;
  }
  if (strcmp(q.cmd, "poll") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    if (s->rpos < s->rlen) return a_scalar(1.0);
    return a_scalar(net_wait(s->fd, rq_has(&q, 2) ? rq_int(&q, 2) : 0) ? 1.0 : 0.0);
  }
  if (strcmp(q.cmd, "status") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    if (!s->eof && s->rpos >= s->rlen && !s->listening && net_wait(s->fd, 0)) net_fill(s, 0);
    return a_scalar(s->eof && s->rpos >= s->rlen ? 1.0 : 0.0);
  }
  if (strcmp(q.cmd, "port") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    net_peer_text(s, 1);
    return a_scalar((double)atoi(s->peer));
  }
  if (strcmp(q.cmd, "peer") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    return res_text(s->peer, (int)strlen(s->peer));
  }
  if (strcmp(q.cmd, "resolve") == 0) {
    struct addrinfo hints, *res;
    char out[96];
    int rc;
    rq_str(&q, 1, host, sizeof host);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0) net_err("cannot resolve the name", rc, 1);
    out[0] = '\0';
    if (res->ai_family == AF_INET)
      inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, out, sizeof out);
    else if (res->ai_family == AF_INET6)
      inet_ntop(AF_INET6, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, out, sizeof out);
    freeaddrinfo(res);
    return res_text(out, (int)strlen(out));
  }
  if (strcmp(q.cmd, "close") == 0) {
    NetSlot *s = net_of(rq_int(&q, 1));
    close(s->fd);
    s->used = 0;
    return a_scalar(1.0);
  }
  if (strcmp(q.cmd, "handles") == 0) {
    int i, n = 0;
    Arr *r;
    for (i = 0; i < NNET; i++) if (ntab[i].used) n++;
    r = a_vec(T_NUM, n);
    n = 0;
    for (i = 0; i < NNET; i++) if (ntab[i].used) r->num[n++] = (double)(i + 1);
    return r;
  }
  rt_error_s("DOMAIN ERROR: TCP has no command ", q.cmd, "");
  return NULL;
}
#else
static Arr *tcp_request(Arr *val)
{
  (void)val;
  rt_error("NET ERROR: TCP/IP is not available on this platform");
  return NULL;
}
#endif
static Arr *tcp_set(const char *var, Arr *val)
{
  (void)var;
  return tcp_request(val);
}

/* the processors above are registered here (called from ap_init) */
static void ap_init_extra(void)
{
  ap_register(100, "FILES", 1, NULL, NULL, files_set, NULL);
  ap_register(124, "SCREEN", 1, NULL, NULL, screen_set, NULL);
  ap_register(160, "TCP", 1, NULL, NULL, tcp_set, NULL);
  ap_register(210, "COMPFILE", 1, NULL, NULL, comp_set, NULL);
}
