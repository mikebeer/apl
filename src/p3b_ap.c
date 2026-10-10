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
#define AP_MAXPROC 32

typedef struct ApProc {
  int id;
  const char *name;
  int installed;                       /* 0: reserved number, no code yet */
  void (*offer)(const char *var);
  Arr *(*get)(const char *var);
  Arr *(*set)(const char *var, Arr *val);
  void (*retract)(const char *var);
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
}

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
  if (p->offer) p->offer(var);
}
static Arr *ap_do_get(int id, const char *var)
{
  ApProc *p = ap_find(id);
  return (p && p->get) ? p->get(var) : NULL;
}
static Arr *ap_do_set(int id, const char *var, Arr *val)
{
  ApProc *p = ap_find(id);
  return (p && p->set) ? p->set(var, val) : NULL;
}
static void ap_do_retract(int id, const char *var)
{
  ApProc *p = ap_find(id);
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
