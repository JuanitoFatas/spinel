#ifndef SP_PROC_H
#define SP_PROC_H
/* sp_proc.h -- sp_Proc / sp_Curry struct layouts + cold ops.
 *
 * sp_proc_call itself (the hot per-call dispatch) stays a plain
 * (non-static) function defined directly in spinel_rt.h -- like
 * sp_sprintf/sp_argv, its body is resolved at the final link against the
 * generated TU, so it doesn't need to move for lib/sp_proc.c to reach it.
 * _sp_proc_poly_args/_sp_proc_poly_ret (the boxed calling-convention side
 * channel) are likewise already non-static SP_TLS globals in
 * spinel_rt.h; only extern declarations are needed here.
 *
 * Proc#>>/<</compose and the Proc-return (non-local return via longjmp)
 * machinery stay in spinel_rt.h: sp_proc_compose_fn calls sp_poly_to_i
 * (a value-dispatch function that's hot in optcarrot and excluded from
 * eviction), and proc-return threads through the TU-local exception-stack
 * globals (sp_exc_top / sp_unwind_kind / sp_proc_ret_head).
 *
 * 0 optcarrot uses for every function below.
 */
#include "sp_types.h"   /* mrb_int, sp_sym, mrb_bool */
#include "sp_gc.h"      /* sp_RbVal, sp_gc_alloc, sp_gc_mark */
#include "sp_alloc.h"   /* sp_PolyArray, sp_box_sym, sp_box_poly_array, sp_raise_cls */

typedef struct sp_Proc { void *fn; void *cap; void (*cap_scan)(void *); mrb_int arity; mrb_bool lambda_p; mrb_int param_count; const sp_sym *param_kinds; const sp_sym *param_names; mrb_bool frozen; /* Object#freeze observed (sp_gc_alloc zero-fills) */ void *origin; /* dup/clone lineage root for Proc#== (NULL: self is the root) */ } sp_Proc;
typedef struct { sp_Proc *target; mrb_int nargs; sp_RbVal args[16]; } sp_Curry;

mrb_int sp_proc_call(sp_Proc *p, mrb_int argc, mrb_int *args);   /* defined in the generated TU */
extern SP_TLS sp_RbVal _sp_proc_poly_args[16];                   /* defined in the generated TU */
extern SP_TLS sp_RbVal _sp_proc_poly_ret;                        /* defined in the generated TU */

/* The lineage root of a proc: dups/clones of one proc share it, so Proc#== /
   #eql? compare roots (a dup == its original) while distinct literals differ. */
static inline sp_Proc *sp_proc_root(sp_Proc *p) { return (p && p->origin) ? (sp_Proc *)p->origin : p; }

void sp_Proc_scan(void *p);
sp_Proc *sp_proc_new_meta(void *fn, void *cap, void (*cap_scan)(void *), mrb_int arity, mrb_bool lambda_p, mrb_int param_count, const sp_sym *param_kinds, const sp_sym *param_names);
sp_Proc *sp_proc_dup(sp_Proc *p, int keep_frozen);
sp_Proc *sp_proc_new(void *fn, void *cap, void (*cap_scan)(void *));
mrb_int sp_proc_arity(sp_Proc *p);
mrb_bool sp_proc_lambda_p(sp_Proc *p);
const char *sp_proc_inspect(sp_Proc *p);
void sp_proc_lambda_arity_check(mrb_int argc, mrb_int req, mrb_int opt, mrb_bool has_rest, mrb_bool has_kw);
sp_PolyArray *sp_proc_parameters_ids(sp_Proc *p, int mode, sp_sym req_id, sp_sym opt_id);
sp_PolyArray *sp_proc_parameters(sp_Proc *p);
void sp_curry_scan(void *p);
sp_Curry *sp_curry_new(sp_Proc *p);
sp_Curry *sp_curry_apply(sp_Curry *c, sp_RbVal arg);
void sp_curry_publish_args(sp_Curry *c);

/* ---- BoundMethod (Method object): sp_bound_method_new is hot in
   optcarrot (69 uses), so it stays static inline here -- pure textual
   move, same per-TU inlining as before. sp_BoundMethod_scan is a GC
   callback (only ever invoked indirectly through the function pointer
   sp_bound_method_new hands to sp_gc_alloc), so moving its body to
   lib/sp_proc.c costs nothing -- taking its address from an inline
   context is just an embedded constant, not a call site. ---- */
/* What `self` holds, so the scan knows whether to follow it: a Method can be
   bound to an Integer or a Float as easily as to an object, and marking the
   raw value as a pointer reads whatever address it names. */
#define SP_BM_SELF_NONE 0   /* not a reference: a number, a class value, unbound */
#define SP_BM_SELF_OBJ  1
#define SP_BM_SELF_STR  2
typedef struct sp_BoundMethod { void *self; mrb_int fn; const char *name; mrb_int arity;
  const char *desc;   /* compile-time #inspect rendering ("#<Method: Owner#name(params)>"), or NULL */
  mrb_int self_kind;  /* SP_BM_SELF_* */
  mrb_int unbound;    /* built by #unbind on a boxed Method: reports UnboundMethod */
} sp_BoundMethod;
void sp_bm_cap_scan(void *p);
mrb_int sp_method_proc_tramp(void *cap, mrb_int argc, mrb_int *args);
sp_Proc *sp_method_to_proc(sp_BoundMethod *m);
void sp_BoundMethod_scan(void *p);

static inline sp_BoundMethod *sp_bound_method_new(void *self, mrb_int self_kind, mrb_int fn, const char *name, mrb_int arity) { sp_BoundMethod *m = (sp_BoundMethod *)sp_gc_alloc(sizeof(sp_BoundMethod), NULL, sp_BoundMethod_scan); m->self = self; m->self_kind = self_kind; m->fn = fn; m->name = name; m->arity = arity; m->desc = NULL; m->unbound = 0; return m; }
static inline sp_BoundMethod *sp_bound_method_new_d(void *self, mrb_int self_kind, mrb_int fn, const char *name, mrb_int arity, const char *desc) { sp_BoundMethod *m = sp_bound_method_new(self, self_kind, fn, name, arity); m->desc = desc; return m; }
/* Method#unbind on a BOXED method: the same target with the receiver dropped.
   A boxed value carries no syntax for the compile-time unbound rendering, so
   the copy records it (the #class arm reads `unbound`) (#3692). */
static inline sp_BoundMethod *sp_bm_unbind(sp_BoundMethod *m) {
  if (!m) return NULL;
  sp_BoundMethod *u = sp_bound_method_new(NULL, SP_BM_SELF_NONE, m->fn, m->name, m->arity);
  u->desc = m->desc;
  u->unbound = 1;
  return u;
}

#endif
