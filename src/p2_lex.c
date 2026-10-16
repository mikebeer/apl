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
