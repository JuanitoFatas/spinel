/* sp_proc.c -- cold sp_Proc/sp_Curry ops (see sp_proc.h). 0 optcarrot uses. */
#include "sp_proc.h"

void sp_Proc_scan(void *p) { sp_Proc *pr = (sp_Proc *)p; if (pr->cap && pr->cap_scan) pr->cap_scan(pr->cap); }
sp_Proc *sp_proc_new_meta(void *fn, void *cap, void (*cap_scan)(void *), mrb_int arity, mrb_bool lambda_p, mrb_int param_count, const sp_sym *param_kinds, const sp_sym *param_names) { sp_Proc *p = (sp_Proc *)sp_gc_alloc(sizeof(sp_Proc), NULL, sp_Proc_scan); p->fn = fn; p->cap = cap; p->cap_scan = cap_scan; p->arity = arity; p->lambda_p = lambda_p; p->param_count = param_count; p->param_kinds = param_kinds; p->param_names = param_names; return p; }
/* Proc#dup / #clone: a fresh shallow copy (distinct identity; the capture
   environment is shared, like CRuby). dup drops the frozen flag, clone keeps
   it (#3048). */
sp_Proc *sp_proc_dup(sp_Proc *p, int keep_frozen) {
  if (!p) return p;
  SP_GC_ROOT(p);
  sp_Proc *r = (sp_Proc *)sp_gc_alloc(sizeof(sp_Proc), NULL, sp_Proc_scan);
  *r = *p;
  if (!keep_frozen) r->frozen = 0;
  r->origin = sp_proc_root(p);   /* share the lineage root so dup == original */
  return r;
}
sp_Proc *sp_proc_new(void *fn, void *cap, void (*cap_scan)(void *)) { return sp_proc_new_meta(fn, cap, cap_scan, 0, FALSE, 0, NULL, NULL); }
mrb_int sp_proc_arity(sp_Proc *p) { return p ? p->arity : 0; }
mrb_bool sp_proc_lambda_p(sp_Proc *p) { return p ? p->lambda_p : FALSE; }
/* Proc#inspect: CRuby prints "#<Proc:0xADDR file:line (lambda)>"; the
   source location is not tracked, so the address form (+ lambda marker) is
   the best-effort rendering. */
const char *sp_proc_inspect(sp_Proc *p) {SP_GC_ROOT(p);
  if (!p) return SPL("nil");
  return sp_sprintf(p->lambda_p ? "#<Proc:0x%016llx (lambda)>" : "#<Proc:0x%016llx>",
                    (unsigned long long)(uintptr_t)p);
}
/* Lambda strict-arity check: raise ArgumentError if argc is outside
   [req, req+opt] (no upper bound with a rest param). Procs are lenient. */
void sp_proc_lambda_arity_check(mrb_int argc, mrb_int req, mrb_int opt, mrb_bool has_rest, mrb_bool has_kw) {
  /* a lambda with keyword parameters accepts one extra trailing argument (the
     keyword hash) beyond its positional maximum. */
  mrb_int max = req + opt + (has_kw ? 1 : 0);
  if (argc < req || (!has_rest && argc > max)) sp_raise_cls("ArgumentError", "wrong number of arguments");
}
/* Proc#parameters with an explicit mode. Kinds are stored canonically
   (lambda-style: a plain positional is "req"); printing for proc mode remaps
   req -> opt, which leaves defaulted positionals (stored "opt") and every
   non-positional kind untouched -- exactly CRuby's parameters(lambda:) rule.
   mode: 1 = lambda view, 0 = proc view, -1 = the receiver's own nature.
   req_id/opt_id are the generated TU's interned ids for those kinds. #2693 */
sp_PolyArray *sp_proc_parameters_ids(sp_Proc *p, int mode, sp_sym req_id, sp_sym opt_id) { SP_GC_ROOT(p);
  sp_PolyArray *r = sp_PolyArray_new();
  if (!p || p->param_count <= 0 || !p->param_kinds) return r;
  SP_GC_ROOT(r);
  int want_lambda = mode >= 0 ? mode : (p->lambda_p ? 1 : 0);
  for (mrb_int i = 0; i < p->param_count; i++) {
    sp_sym k = p->param_kinds[i];
    if (!want_lambda && k == req_id) k = opt_id;
    sp_PolyArray *pair = sp_PolyArray_new();
    sp_PolyArray_push(pair, sp_box_sym(k));
    if (p->param_names && p->param_names[i] >= 0) sp_PolyArray_push(pair, sp_box_sym(p->param_names[i]));
    sp_PolyArray_push(r, sp_box_poly_array(pair));
  }
  return r;
}
sp_PolyArray *sp_proc_parameters(sp_Proc *p) { SP_GC_ROOT(p); sp_PolyArray *r = sp_PolyArray_new(); if (!p || p->param_count <= 0 || !p->param_kinds) return r; SP_GC_ROOT(r); for (mrb_int i = 0; i < p->param_count; i++) { sp_PolyArray *pair = sp_PolyArray_new(); sp_PolyArray_push(pair, sp_box_sym(p->param_kinds[i])); if (p->param_names && p->param_names[i] >= 0) sp_PolyArray_push(pair, sp_box_sym(p->param_names[i])); sp_PolyArray_push(r, sp_box_poly_array(pair)); } return r; }
void sp_curry_scan(void *p) { sp_Curry *c = (sp_Curry *)p; if (c->target) sp_gc_mark(c->target); for (mrb_int i = 0; i < c->nargs && i < 16; i++) sp_mark_rbval(c->args[i]); }
sp_Curry *sp_curry_new(sp_Proc *p) {
  SP_GC_ROOT(p);  /* the target proc has no other root across this alloc */
  sp_Curry *c = (sp_Curry *)sp_gc_alloc(sizeof(sp_Curry), NULL, sp_curry_scan);
  c->target = p; c->nargs = 0;
  return c;
}
/* Each accumulated argument is stored boxed so a non-int arg (a String, an
   object, ...) keeps its type through the deferred call (#3183). */
sp_Curry *sp_curry_apply(sp_Curry *c, sp_RbVal arg) {
  /* root the source accumulator: in a chained apply it is only referenced
     from a C argument slot, and this allocation can collect */
  SP_GC_ROOT(c);
  sp_Curry *n = (sp_Curry *)sp_gc_alloc(sizeof(sp_Curry), NULL, sp_curry_scan);
  *n = *c;
  if (n->nargs < 16) n->args[n->nargs++] = arg;
  return n;
}
/* Publish the accumulated (boxed) args on the side-channel: a poly-param
   target reads its arguments back from there, keeping each arg's real type. */
void sp_curry_publish_args(sp_Curry *c) {
  for (mrb_int i = 0; i < c->nargs && i < 16; i++)
    _sp_proc_poly_args[i] = c->args[i];
}

/* Method#to_proc: wrap the bound method in a Proc whose trampoline forwards
   through the (void *self, mrb_int...) ABI (the arity dispatches the cast). */
void sp_bm_cap_scan(void *p) { sp_gc_mark(p); }
mrb_int sp_method_proc_tramp(void *cap, mrb_int argc, mrb_int *args) {
  sp_BoundMethod *m = (sp_BoundMethod *)cap;
  if (!m || !m->fn) return 0;
  switch (argc) {
    case 0: return ((mrb_int (*)(void *))(uintptr_t)m->fn)(m->self);
    case 1: return ((mrb_int (*)(void *, mrb_int))(uintptr_t)m->fn)(m->self, args[0]);
    case 2: return ((mrb_int (*)(void *, mrb_int, mrb_int))(uintptr_t)m->fn)(m->self, args[0], args[1]);
    case 3: return ((mrb_int (*)(void *, mrb_int, mrb_int, mrb_int))(uintptr_t)m->fn)(m->self, args[0], args[1], args[2]);
    default: return ((mrb_int (*)(void *, mrb_int, mrb_int, mrb_int, mrb_int))(uintptr_t)m->fn)(m->self, args[0], args[1], args[2], args[3]);
  }
}
sp_Proc *sp_method_to_proc(sp_BoundMethod *m) {
  return sp_proc_new_meta((void *)sp_method_proc_tramp, m, sp_bm_cap_scan, 1, TRUE, 0, NULL, NULL);
}
/* Bound Method object: `obj.method(:foo)` / `method(:foo)`. `self` is the
   bound receiver (NULL for a top-level method), `fn` the function address
   (cast to the right signature at the call site), `name` the method name
   (a string literal). Only `self` is GC-managed. */
void sp_BoundMethod_scan(void *p) { sp_BoundMethod *m = (sp_BoundMethod *)p; if (m->self) sp_gc_mark(m->self); }
