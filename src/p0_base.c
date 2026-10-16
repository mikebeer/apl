/* p0_base.c - portable base layer: includes, types, memory, errors.
 * Strict C89. No codepoint arithmetic (EBCDIC-safe). No 64-bit ints.
 */
#if defined(__unix__) || defined(__unix) || defined(__linux__) || \
    defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#define APL_HAVE_DIRENT 1
#define APL_HAVE_SOCKETS 1
#define APL_HAVE_TERMIOS 1
#endif
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>

#ifdef APL_HAVE_SOCKETS
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#endif
#ifdef APL_HAVE_TERMIOS
#include <termios.h>
#endif

#ifdef __CMS__
/* GCCLIB31's setjmp.h declares only jmp_buf; the routines are in assembler */
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif

#define APL_VERSION "0.1"
#ifndef APL_MAXELEMS
#define APL_MAXELEMS 16777216L   /* largest array, in elements */
#endif
#define MAXRANK 8
#define MAXLINE 1024
#define MAXCATCH 32
#define MAXDEPTH 200

/* ---- character classes (never use ranges for letters: EBCDIC) ---- */
static const char LETTERS[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char UPPER[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char LOWER[] = "abcdefghijklmnopqrstuvwxyz";

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_alpha(int c) { return c != 0 && strchr(LETTERS, c) != NULL; }
static int is_alnum(int c) { return is_digit(c) || is_alpha(c); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

static int to_upper(int c)
{
  const char *p = (c != 0) ? strchr(LOWER, c) : NULL;
  return p ? UPPER[p - LOWER] : c;
}

/* ---- error handling: setjmp/longjmp catch stack ---- */
static jmp_buf catch_jb[MAXCATCH];
static int catch_top = -1;
static char errkind[32];
static char errmsg[256];

static void unwind_frames(int depth);   /* defined in the evaluator */

static void fail(const char *kind, const char *msg)
{
  strncpy(errkind, kind, sizeof(errkind) - 1);
  errkind[sizeof(errkind) - 1] = '\0';
  strncpy(errmsg, msg, sizeof(errmsg) - 1);
  errmsg[sizeof(errmsg) - 1] = '\0';
  if (catch_top < 0) {
    fprintf(stderr, "apl: %s error -- %s\n", errkind, errmsg);
    exit(2);
  }
  longjmp(catch_jb[catch_top], 1);
}
static void rt_error(const char *msg) { fail("runtime", msg); }
static void syn_error(const char *msg) { fail("syntax", msg); }
static void rt_error_s(const char *a, const char *b, const char *c)
{
  char buf[256];
  sprintf(buf, "%.60s%.100s%.60s", a, b, c);
  fail("runtime", buf);
}
static void syn_error_s(const char *a, const char *b)
{
  char buf[256];
  sprintf(buf, "%.80s%.120s", a, b);
  fail("syntax", buf);
}

/* ---- memory ---- */
static void *xmalloc(size_t n)
{
  void *p = malloc(n ? n : 1);
  if (!p) rt_error("WS FULL");
  return p;
}
static void *xrealloc(void *q, size_t n)
{
  void *p = realloc(q, n ? n : 1);
  if (!p) rt_error("WS FULL");
  return p;
}
static char *xstrdup(const char *s)
{
  size_t n = strlen(s);
  char *p = (char *)xmalloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}
static char *xstrndup(const char *s, int n)
{
  char *p = (char *)xmalloc((size_t)n + 1);
  memcpy(p, s, (size_t)n);
  p[n] = '\0';
  return p;
}

/* ---- interpreter-wide settings ---- */
static int Q_IO = 1;          /* []IO */
static int Q_PP = 6;          /* []PP */
#define APL_NUM_MAX 7.0e75   /* hex FP on VM/370 tops out near 7.2e75 */
static double Q_CT = 1e-13;   /* []CT comparison tolerance */
static unsigned long Q_RL = 16807UL; /* []RL random seed (32 bit) */
static char wsid[128] = "CLEAR WS";
static int interactive = 0;   /* 1 when reading a terminal REPL */
