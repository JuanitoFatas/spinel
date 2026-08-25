/*
** re_lit_check.c - compile a regexp literal at COMPILE time, to refuse a
** pattern the engine cannot read before the program is built.
**
** A literal used to reach the engine only at program startup, so `/[z-a]/`
** built cleanly and raised RegexpError when the program ran. CRuby reports it
** as a SyntaxError from the parse, and an AOT compiler holds the same
** information the parser does: the pattern and its flags are both constants.
** So the compiler links the engine and asks it.
*/

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "re_internal.h"

/* The engine's #inspect / #to_s helpers call sp_sprintf, which the GENERATED
   program defines for itself. Nothing on the validation path reaches those,
   but the object file carries them, so the compiler needs a definition of its
   own to link. It is a real one rather than an abort stub: a stub would turn
   a future caller into a crash, where this only ever costs a malloc. */
const char *sp_sprintf(const char *fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) { va_end(ap2); return ""; }
  char *s = (char *)malloc((size_t)n + 1);
  if (!s) { va_end(ap2); return ""; }
  vsnprintf(s, (size_t)n + 1, fmt, ap2);
  va_end(ap2);
  return s;
}

/* The engine reports a compile error through a handler that must not return.
   Catch it, keep the message, and hand it back to the caller. */
static char re_lit_msg[512];
static jmp_buf re_lit_jmp;

static void re_lit_error(const char *msg) {
  snprintf(re_lit_msg, sizeof re_lit_msg, "%s", msg);
  longjmp(re_lit_jmp, 1);
}

/* Compile `src` with `flags` and throw the result away. Returns NULL when the
   pattern is one the engine reads, and the error message otherwise. The
   message is the engine's own, so it reads as it would have at run time,
   flag suffix included.

   The handler is restored to whatever it was, which is nothing in the
   compiler: leaving it installed would send a later failure through a longjmp
   whose jmp_buf has gone out of scope. */
const char *sp_re_literal_error(const char *src, int len, int flags) {
  re_lit_msg[0] = 0;
  sp_re_set_error_handler(re_lit_error);
  const char *err = NULL;
  if (setjmp(re_lit_jmp) == 0) {
    mrb_regexp_pattern *pat = re_compile(src, (mrb_int)len, (uint32_t)flags);
    if (pat) re_free(pat);
    else err = "regexp could not be compiled";
  }
  else {
    err = re_lit_msg;
  }
  sp_re_set_error_handler(NULL);
  return err;
}
