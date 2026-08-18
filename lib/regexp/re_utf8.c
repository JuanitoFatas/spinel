/*
** re_utf8.c - UTF-8 utility functions for regexp engine
**
** See Copyright Notice in mruby.h
*/

#include "re_internal.h"

/* Return byte length of UTF-8 character at s.
   Returns 1 for invalid sequences (treat as single byte). */
int
re_utf8_charlen(const char *s, const char *end)
{
  uint8_t c = (uint8_t)*s;
  int len;

  if (c < 0x80) return 1;
  else if (c < 0xc0) return 1;  /* invalid continuation */
  else if (c < 0xe0) len = 2;
  else if (c < 0xf0) len = 3;
  else if (c < 0xf8) len = 4;
  else return 1;  /* invalid */

  if (s + len > end) return 1;  /* truncated */
  return len;
}

/* Decode a UTF-8 character and return its codepoint.
   *len is set to the byte length consumed.
   The buffer end is checked before any continuation byte is read
   (imported from mruby): a truncated multi-byte leader at the end of
   a pattern or subject consumes a single byte instead of reading past
   the buffer, mirroring re_utf8_charlen.
   Issue #780: reject overlong sequences (where a multi-byte form
   encodes a codepoint that fits in fewer bytes). RFC 3629 + Ruby
   both treat these as invalid. Returns U+FFFD (replacement) and
   consumes just the lead byte so the caller can resync. */
uint32_t
re_utf8_decode(const char *s, const char *end, int *len)
{
  uint8_t c = (uint8_t)s[0];
  uint32_t cp;

  if (c < 0x80) {
    *len = 1;
    return c;
  }
  else if (c < 0xc0) {
    *len = 1;
    return c;  /* invalid lead byte, return as-is */
  }
  else if (c < 0xe0) {
    /* Issue #754: validate continuation byte before reading further.
       Rejects any byte without the 10xxxxxx continuation pattern. */
    if (s + 2 > end || ((uint8_t)s[1] & 0xc0) != 0x80) { *len = 1; return 0xFFFD; }
    *len = 2;
    cp = (c & 0x1f) << 6;
    cp |= ((uint8_t)s[1] & 0x3f);
    if (cp < 0x80) { *len = 1; return 0xFFFD; }
    return cp;
  }
  else if (c < 0xf0) {
    if (s + 3 > end ||
        ((uint8_t)s[1] & 0xc0) != 0x80 || ((uint8_t)s[2] & 0xc0) != 0x80) {
      *len = 1; return 0xFFFD;
    }
    *len = 3;
    cp = (c & 0x0f) << 12;
    cp |= ((uint8_t)s[1] & 0x3f) << 6;
    cp |= ((uint8_t)s[2] & 0x3f);
    if (cp < 0x800) { *len = 1; return 0xFFFD; }
    return cp;
  }
  else {
    if (s + 4 > end ||
        ((uint8_t)s[1] & 0xc0) != 0x80 || ((uint8_t)s[2] & 0xc0) != 0x80 ||
        ((uint8_t)s[3] & 0xc0) != 0x80) {
      *len = 1; return 0xFFFD;
    }
    *len = 4;
    cp = (c & 0x07) << 18;
    cp |= ((uint8_t)s[1] & 0x3f) << 12;
    cp |= ((uint8_t)s[2] & 0x3f) << 6;
    cp |= ((uint8_t)s[3] & 0x3f);
    if (cp < 0x10000 || cp > 0x10FFFF) { *len = 1; return 0xFFFD; }
    return cp;
  }
}

/* Check if character is a "word" character (\w): [a-zA-Z0-9_] */
mrb_bool
re_is_word_char(uint32_t c)
{
  if (c >= 'a' && c <= 'z') return TRUE;
  if (c >= 'A' && c <= 'Z') return TRUE;
  if (c >= '0' && c <= '9') return TRUE;
  if (c == '_') return TRUE;
  return FALSE;
}

/* Encode a codepoint as UTF-8 into buf and return the byte length, at most 4.
   Callers reject a surrogate and anything above U+10FFFF before they get
   here, so every input has an encoding. (ported from mruby-regexp 048e5da5f) */
int
re_utf8_encode(uint32_t cp, char *buf)
{
  if (cp < 0x80) {
    buf[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    buf[0] = (char)(0xc0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    buf[0] = (char)(0xe0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    buf[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  buf[0] = (char)(0xf0 | (cp >> 18));
  buf[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  buf[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
  buf[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

/* ---- simple case folding (see re_internal.h) ---- */
#ifdef RE_UNICODE_CASE
#include "re_casefold.h"

static mrb_bool
fold_run_holds(const re_fold_run *r, uint32_t cp)
{
  return cp >= r->lo && cp <= r->hi && ((cp - r->lo) % r->stride) == 0;
}

uint32_t
re_case_fold(uint32_t cp)
{
  for (int i = 0; i < RE_FOLD_RUN_COUNT; i++) {
    const re_fold_run *r = &re_fold_runs[i];
    if (cp < r->lo) break;               /* runs are sorted by lo */
    if (fold_run_holds(r, cp)) return (uint32_t)((int32_t)cp + r->delta);
  }
  return cp;
}

int
re_case_alts(uint32_t cp, uint32_t *out)
{
  uint32_t f = re_case_fold(cp);
  int n = 0;
  out[n++] = f;
  /* every source that folds to f is a counterpart; the walk is over runs
     rather than codepoints, so a wide range costs the run count */
  for (int i = 0; i < RE_FOLD_RUN_COUNT && n < RE_CASE_ALTS_MAX; i++) {
    const re_fold_run *r = &re_fold_runs[i];
    int64_t src = (int64_t)f - r->delta;
    if (src < 0) continue;
    if (!fold_run_holds(r, (uint32_t)src)) continue;
    if ((uint32_t)src == f) continue;
    out[n++] = (uint32_t)src;
  }
  return n;
}
#else
uint32_t
re_case_fold(uint32_t cp)
{
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  return cp;
}

int
re_case_alts(uint32_t cp, uint32_t *out)
{
  uint32_t f = re_case_fold(cp);
  out[0] = f;
  if (f >= 'a' && f <= 'z') { out[1] = f - 32; return 2; }
  return 1;
}
#endif  /* RE_UNICODE_CASE */
