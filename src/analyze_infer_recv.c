/* analyze_infer_recv.c -- receiver-typed call inference, split out of
   infer_call. Pure code movement, no logic change: each helper holds the arms
   for one receiver kind, in their original order, and infer_call calls it at
   the point those arms occupied.

   The helpers report `1 = handled` with the type through `out` rather than
   returning a TyKind, because TY_UNKNOWN is a real answer here -- a String
   range's whole traversal face deliberately answers UNKNOWN so the element
   array serves it -- and a bare TyKind return could not tell that from
   "declined". */
#include "analyze_internal.h"
#include <stdio.h>
#include <string.h>

/* Range receivers: the Float and String range faces, and the Integer-range
   arms that answer without materializing. The redispatch that rewrites `rt`
   to the int array stays in infer_call: it changes the receiver kind for
   every arm after it rather than answering. */
int infer_range_call(Compiler *c, int id, TyKind rt, TyKind *out) {
  const NodeTable *nt = c->nt;
  const char *name = nt_str(nt, id, "name");
  int recv = nt_ref(nt, id, "receiver");
  int args = nt_ref(nt, id, "arguments");
  int argc = 0;
  const int *argv = NULL;
  if (args >= 0) argv = nt_arr(nt, args, "arguments", &argc);
  (void)argv; (void)recv;
  if (!name) return 0;
  /* A Float range (1.0..3.0) is a distinct type with float endpoints; it is
     not iterable, so its whole method face reduces to endpoint queries,
     membership tests, and the sole materializing method, step. */
  /* String range ("a".."e"): the endpoints answer natively; every traversal
     rides the materialized element array (#3064). */
  if (rt == TY_STR_RANGE) {
    if (sp_streq(name, "begin") || sp_streq(name, "end") ||
        sp_streq(name, "min") || sp_streq(name, "max") ||
        sp_streq(name, "to_s") || sp_streq(name, "inspect"))
      { *out = argc == 0 ? TY_STRING : TY_STR_ARRAY; return 1; }
    if ((sp_streq(name, "first") || sp_streq(name, "last")))
      { *out = argc == 0 ? TY_STRING : TY_STR_ARRAY; return 1; }
    if (sp_streq(name, "cover?") || sp_streq(name, "include?") ||
        sp_streq(name, "member?") || sp_streq(name, "===") ||
        sp_streq(name, "==") || sp_streq(name, "eql?") ||
        sp_streq(name, "exclude_end?") || sp_streq(name, "frozen?") ||
        sp_streq(name, "nil?") || sp_streq(name, "is_a?") ||
        sp_streq(name, "kind_of?") || sp_streq(name, "instance_of?") ||
        sp_streq(name, "equal?") || sp_streq(name, "respond_to?"))
      { *out = TY_BOOL; return 1; }
    /* step(n) / %(n): an Enumerator over every nth member (#3671) */
    if ((sp_streq(name, "step") || sp_streq(name, "%")) && argc == 1 &&
        nt_ref(nt, id, "block") < 0)
      { *out = TY_ENUMERATOR; return 1; }
    if (sp_streq(name, "class")) { *out = TY_CLASS; return 1; }
    if (sp_streq(name, "hash")) { *out = TY_INT; return 1; }
    /* Range#size counts INTEGER elements, so a string range has none: nil
       (CRuby), not the materialized array's length. */
    if (sp_streq(name, "size") && argc == 0) { *out = TY_NIL; return 1; }
    if ((sp_streq(name, "to_a") || sp_streq(name, "entries")) && argc == 0)
      { *out = TY_STR_ARRAY; return 1; }
    if (sp_streq(name, "freeze") || sp_streq(name, "itself") ||
        sp_streq(name, "dup") || sp_streq(name, "clone"))
      { *out = TY_STR_RANGE; return 1; }
    /* everything else is served by the element array (see the desugar) */
    { *out = TY_UNKNOWN; return 1; }
  }
  if (rt == TY_FLOAT_RANGE) {
    /* #size counts the integers the range enumerates: a Float answer, since an
       unbounded end makes it Infinity (#3670). Only an Integer begin has an
       enumeration at all; the emitter checks that and leaves the rest to the
       TypeError CRuby raises. */
    if ((sp_streq(name, "size") || sp_streq(name, "count")) && argc == 0 &&
        nt_ref(nt, id, "block") < 0) {
      int rq3 = nt_ref(nt, id, "receiver");
      while (rq3 >= 0 && nt_kind(nt, rq3) == NK_ParenthesesNode) {
        int pb3 = nt_ref(nt, rq3, "body"); int pn3 = 0;
        const int *pd3 = pb3 >= 0 ? nt_arr(nt, pb3, "body", &pn3) : NULL;
        rq3 = (pn3 == 1 && pd3) ? pd3[0] : -1;
      }
      int lo3 = (rq3 >= 0 && nt_kind(nt, rq3) == NK_RangeNode) ? nt_ref(nt, rq3, "left") : -1;
      if (lo3 >= 0 && infer_type(c, lo3) == TY_INT) { *out = TY_FLOAT; return 1; }
    }
    if (sp_streq(name, "begin") || sp_streq(name, "end") ||
        sp_streq(name, "first") || sp_streq(name, "last") ||
        sp_streq(name, "min") || sp_streq(name, "max")) {
      /* A range is a Float range when EITHER endpoint is one, and the endpoint
         methods answer the endpoint the caller wrote: `(-Float::INFINITY..5).max`
         is the Integer 5 (#3837). */
      if (argc == 0) {
        int rnode = recv;
        for (int g = 0; g < 8 && rnode >= 0 && nt_kind(nt, rnode) == NK_ParenthesesNode; g++) {
          int pb2 = nt_ref(nt, rnode, "body");
          int pn2 = 0; const int *ps2 = pb2 >= 0 ? nt_arr(nt, pb2, "body", &pn2) : NULL;
          rnode = (pn2 == 1 && ps2) ? ps2[0] : -1;
        }
        if (rnode >= 0 && nt_kind(nt, rnode) == NK_RangeNode) {
          int side = (sp_streq(name, "begin") || sp_streq(name, "first") ||
                      sp_streq(name, "min")) ? nt_ref(nt, rnode, "left") : nt_ref(nt, rnode, "right");
          if (side >= 0 && infer_type(c, side) == TY_INT) { *out = TY_INT; return 1; }
        }
      }
      { *out = argc == 0 ? TY_FLOAT : TY_POLY; return 1; }   /* first(n)/last(n) raise anyway */
    }
    if (sp_streq(name, "cover?") || sp_streq(name, "include?") ||
        sp_streq(name, "member?") || sp_streq(name, "===") ||
        sp_streq(name, "==") || sp_streq(name, "eql?") ||
        sp_streq(name, "exclude_end?") || sp_streq(name, "frozen?") ||
        sp_streq(name, "respond_to?") || sp_streq(name, "nil?") ||
        sp_streq(name, "is_a?") || sp_streq(name, "kind_of?") ||
        sp_streq(name, "instance_of?") || sp_streq(name, "equal?"))
      { *out = TY_BOOL; return 1; }
    if (sp_streq(name, "to_s") || sp_streq(name, "inspect")) { *out = TY_STRING; return 1; }
    if (sp_streq(name, "minmax") && argc == 0) { *out = TY_FLOAT_ARRAY; return 1; }  /* the endpoints (#3690) */
    if (sp_streq(name, "step")) { *out = TY_FLOAT_ARRAY; return 1; }
    if (sp_streq(name, "bsearch") && nt_ref(nt, id, "block") >= 0) { *out = TY_FLOAT; return 1; }
    if (sp_streq(name, "class")) { *out = TY_CLASS; return 1; }
    if (sp_streq(name, "freeze") || sp_streq(name, "itself") ||
        sp_streq(name, "dup") || sp_streq(name, "clone"))
      { *out = TY_FLOAT_RANGE; return 1; }
    /* each/map/sum/to_a/... raise "can't iterate from Float" at run time; a
       poly result keeps the boxed-nil slot the raise leaves behind valid (and
       lets respond_to? report these Enumerable methods as present, like CRuby).
       A name outside this set is genuinely undefined, so leave it UNKNOWN: the
       respond_to? probe reads that as "not dispatchable" (false), matching an
       ordinary int range, and a real call errors like any unknown method. */
    {
      static const char *const iter[] = {
        "each", "map", "collect", "select", "filter", "reject", "to_a", "to_h",
        "entries", "find", "detect", "find_index", "count", "sum", "sort",
        "sort_by", "min_by", "max_by", "reduce", "inject", "each_with_index",
        "flat_map", "collect_concat", "any?", "all?", "none?", "one?", "take",
        "drop", "take_while", "drop_while", "filter_map", "partition",
        "group_by", "each_with_object", "tally", "find_all", "zip", "grep",
        "grep_v", "uniq", "reverse", "minmax", "join", "index", "size", "lazy",
        "each_cons", "each_slice", "chunk", "chunk_while", "cycle", NULL };
      for (int k = 0; iter[k]; k++) if (sp_streq(name, iter[k])) { *out = TY_POLY; return 1; }
    }
    { *out = TY_UNKNOWN; return 1; }
  }
  /* endless literal range: size is the Float infinity; take/first with a
     count materialize just the counted prefix (nothing else can) */
  /* (1..5.5): the end readers answer the Float the caller wrote; the integer
     representation truncated it (#3896). */
  if (rt == TY_RANGE && recv >= 0 && argc == 0 && nt_ref(nt, id, "block") < 0 &&
      (sp_streq(name, "end") || sp_streq(name, "last") || sp_streq(name, "max")) &&
      range_lit_float_end(c, recv) >= 0)
    { *out = TY_FLOAT; return 1; }
  if (rt == TY_RANGE && recv >= 0) {
    int rnA = recv;
    while (rnA >= 0 && nt_type(nt, rnA) && sp_streq(nt_type(nt, rnA), "ParenthesesNode")) {
      int pbA = nt_ref(nt, rnA, "body"); int pnA = 0;
      const int *ppA = pbA >= 0 ? nt_arr(nt, pbA, "body", &pnA) : NULL;
      rnA = pnA == 1 ? ppA[0] : -1;
    }
    /* a local holding only such a literal counts too (sole-assignment) */
    if (rnA >= 0 && nt_type(nt, rnA) && !sp_streq(nt_type(nt, rnA), "RangeNode")) {
      int slA = local_sole_range_node(c, rnA);
      if (slA >= 0) rnA = slA;
    }
    if (rnA >= 0 && nt_type(nt, rnA) && sp_streq(nt_type(nt, rnA), "RangeNode") &&
        (nt_ref(nt, rnA, "right") < 0 ||
         infer_end_is_float_inf(c, nt_ref(nt, rnA, "right"))) &&
        nt_ref(nt, rnA, "left") >= 0) {
      if ((sp_streq(name, "size") || sp_streq(name, "count")) && argc == 0 &&
          nt_ref(nt, id, "block") < 0)
        { *out = TY_FLOAT; return 1; }   /* an endless range counts forever: Infinity (#3668) */
      if ((sp_streq(name, "take") || sp_streq(name, "first")) && argc == 1)
        { *out = TY_INT_ARRAY; return 1; }
      /* the block forms that walk up from the bounded end rather than
         materializing: the elements are the range's own ints (#3863) */
      if (nt_ref(nt, id, "block") >= 0 && argc == 0) {
        if (sp_streq(name, "find") || sp_streq(name, "detect")) { *out = TY_INT; return 1; }
        if (sp_streq(name, "take_while")) { *out = TY_INT_ARRAY; return 1; }
      }
    }
  }
  /* min(n) / max(n) on an Integer Range answer an Array of its ints, however
     the Range is bounded (#3665) */
  if (rt == TY_RANGE && recv >= 0 && argc == 1 && nt_ref(nt, id, "block") < 0 &&
      (sp_streq(name, "min") || sp_streq(name, "max")))
    { *out = TY_INT_ARRAY; return 1; }
  if (rt == TY_RANGE && sp_streq(name, "sum") && argc == 1 &&
      nt_ref(nt, id, "block") < 0)
    { *out = infer_type(c, argv[0]) == TY_FLOAT ? TY_FLOAT : TY_INT; return 1; }
  return 0;
}
