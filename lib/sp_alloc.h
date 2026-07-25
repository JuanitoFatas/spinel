#ifndef SP_ALLOC_H
#define SP_ALLOC_H
/* Shared GC-string allocation.

   The string-heap state below is `extern` -- defined once in sp_alloc.c and
   linked from libspinel_rt.a -- so the generated program's single translation
   unit and every standalone lib C file share ONE string heap. A cold runtime C file
   (marshal, pack, json, ...) can therefore allocate GC-tracked strings directly
   without #including sp_runtime.h and inheriting its per-TU `static` heap state
   (which would otherwise create a second, never-swept string heap and leak).

   The hot allocators stay `static inline` here so each including TU still
   inlines them over the shared extern state -- the same shape sp_gc_alloc
   already uses for the extern sp_gc_heap / sp_gc_bytes object heap. */
#include "sp_types.h"   /* sp_str_hdr, mrb_int, mrb_float */
#include "sp_gc.h"      /* sp_gc_collect, sp_oom_die, sp_gc_str_sweep_hook */
#include "sp_time.h"    /* sp_Time for sp_box_time */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>      /* snprintf for the int/float formatters below */
#include <math.h>       /* HUGE_VAL / signbit for sp_float_to_s */

const char *sp_sprintf(const char *fmt, ...);  /* defined in the generated TU */

/* Global heap lock (Phase 1, design 6.1). Under SP_THREADS one mutex serializes
   the object- and string-heap mutations -- the trigger+collect, the calloc/
   malloc, and the list link -- so N>1 workers cannot corrupt the shared heap
   lists or byte counters. It wraps the hot allocators here (sp_str_alloc, in
   this header so the generated TU's inlined copy locks too) and in sp_alloc.c
   (sp_gc_alloc). The internal collect/sweep run under the held lock and never
   re-acquire it (no allocator re-enters from a scan/finalize callback). A single
   non-recursive mutex covers both heaps since a collection from either path
   sweeps the other under the same held lock. No-op (and no pthread dependency)
   in the single-threaded build, so that path is byte-identical.
   NOTE: at N>1 this alone is not sufficient -- a worker doing pure computation
   never reaches the lock, so stop-the-world via safepoints (added with the
   workers) is still required before the collector may run. */
#ifdef SP_THREADS
#include <pthread.h>
extern pthread_mutex_t sp_heap_lock;
#define SP_HEAP_LOCK()   pthread_mutex_lock(&sp_heap_lock)
#define SP_HEAP_UNLOCK() pthread_mutex_unlock(&sp_heap_lock)
#else
#define SP_HEAP_LOCK()   ((void)0)
#define SP_HEAP_UNLOCK() ((void)0)
#endif

/* ---- shared string-heap state (defined in sp_alloc.c) ----
   Threaded build: the string heap is per-worker (one singly-linked list + live-
   byte counter per OS worker), so sp_str_alloc pushes lock-free onto its own
   worker's list -- the shared sp_heap_lock serialized every string allocation
   and made allocation-heavy parallel workloads scale NEGATIVELY. A started green
   thread is pinned to its worker (home_wid), so only that worker's M ever pushes
   onto its list, and it pumps one green thread at a time: no concurrent push.
   The collector reaches every list under stop-the-world (all workers parked).
   The single-threaded build keeps one global list and stays byte-identical. */
/* Generations. Allocation always pushes onto the YOUNG list; a string still
   live at a sweep moves to the OLD list, which ordinary (minor) sweeps then
   skip. Without this every sweep walks the entire live set, so a program
   holding a large one -- a Merkle tree of hex digests, a parsed document --
   pays that walk again on each collection no matter how little of it changed.
   Strings have no outgoing references, so there is nothing here of the write
   barrier and forwarding a generational OBJECT heap would need: the split is
   two list heads and a byte counter.
   Reclamation of old strings is deferred to a major sweep, which runs when the
   old generation has itself grown past a threshold. */
#ifdef SP_THREADS
/* sp_worker_id / sp_active_workers / SP_MAX_WORKERS: sp_types.h */
extern sp_str_hdr *sp_str_heap_w[SP_MAX_WORKERS];  /* per-worker young lists */
extern size_t sp_str_heap_bytes_w[SP_MAX_WORKERS]; /* per-worker young bytes */
extern sp_str_hdr *sp_str_old_w[SP_MAX_WORKERS];   /* per-worker old lists */
extern size_t sp_str_old_bytes_w[SP_MAX_WORKERS];  /* per-worker old bytes */
#else
extern sp_str_hdr *sp_str_heap;          /* young list head */
extern size_t sp_str_heap_bytes;         /* young string-heap bytes */
extern sp_str_hdr *sp_str_old;           /* old list head */
extern size_t sp_str_old_bytes;          /* old string-heap bytes */
#endif
extern size_t sp_str_old_threshold;      /* old bytes that trigger a major sweep */
extern size_t sp_str_old_threshold_init; /* recompute floor for the above */
extern size_t sp_str_threshold;          /* string-GC trigger (own heuristic) */
extern size_t sp_str_threshold_init;     /* recompute floor */
extern int    sp_str_stress_checked;     /* one-shot SPINEL_GC_STRESS check */
#ifdef SP_THREADS
void sp_alloc_stress_init(void);         /* race-free one-shot stress check (pre-helpers) */
#endif

extern const char sp_str_empty_data[];
#define sp_str_empty (sp_str_empty_data + 1)

/* UTF-8 char-length cache. Shared (extern) so sp_str_sweep flushes the same
   table the length helpers in sp_runtime.h populate: a per-TU split would leave
   the generated TU's cache pointing at strings the archive-side sweep already
   freed. */
#define SP_STR_LCACHE_BITS 5
#define SP_STR_LCACHE_SIZE (1u << SP_STR_LCACHE_BITS)
struct sp_str_lcache_entry {
  const char *s;
  size_t byte_len;
  mrb_int char_len;
};
/* Per-worker (SP_TLS) in the threaded build: this string-length cache is keyed
   by string pointer and written without the heap lock (sp_str_byte_len is on the
   hot path), so a shared copy would be a data race across workers -- a torn read
   yields a wrong length and sp_str_concat's memcpy overruns. Each worker keeps
   its own; it is cleared at every safepoint park (before a sweep can recycle a
   cached string's address) and by the string sweep on the collector. */
extern SP_TLS struct sp_str_lcache_entry sp_str_lcache[SP_STR_LCACHE_SIZE];
/* Deep-return side channel (#3227): a method whose every return path yields
   a shared-mutable string publishes the sp_String* handle here as the copy
   is read out; a demand-marked call site picks it up right after the call
   (resetting it to NULL first). Never read unless the analysis marked the
   site, so ordinary callers are untouched. */
extern SP_TLS void *_sp_ret_strbuf;

/* Cold; single definitions in sp_alloc.c. sp_str_sweep is wired to the GC via a
   constructor so it runs from sp_gc_collect regardless of which TU triggered
   the collection. */
void sp_str_sweep(void);
void sp_str_lcache_clear(void);
/* Collect + retune (see sp_alloc.c). The single-threaded allocators call the
   per-heap variants directly; sp_stw_collect (threaded) runs _all under STW. */
void sp_str_collect_retune(void);
void sp_gc_collect_retune_all(void);
int  sp_gc_collection_wanted(void);
void sp_stw_collect(void);   /* lib/sp_sched.c: stop-the-world collect (threaded) */

/* SPINEL_ALLOC_REPORT counters (#1336): defined in sp_alloc.c. The `on` flag
   is set once by a constructor from the env var; the hot entry points guard
   their count call on it (one predictable branch when off). */
extern int sp_alloc_report_on;
void sp_alloc_report_count(void *scan, size_t bytes);
void sp_alloc_report_str(size_t bytes);
void sp_alloc_report_tag(void *scan, const char *name);

static inline char *sp_str_alloc(size_t len) {
  size_t total = sizeof(sp_str_hdr) + 1 + len + 1;
  sp_str_hdr *h;
  /* String-heap pressure drives its own collection (see sp_str_heap_bytes).
     Collect BEFORE the new allocation, like sp_gc_alloc, so the string being
     built isn't yet live during the sweep. Operands of the calling op (e.g. the
     arguments to sp_str_concat) must be reachable across this point -- they are
     for rooted locals; the codegen's SP_GC_ROOT discipline is what keeps them
     so. Threshold recompute mirrors sp_gc_alloc's. */
#ifdef SP_THREADS
  int wid = sp_worker_id;
  if (!sp_str_stress_checked) { sp_str_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { sp_str_threshold = 2048; sp_str_threshold_init = 2048; } }
  /* Per-worker trigger: the fast path reads only this worker's own byte count,
     no shared state. Each worker fires at the full threshold, so the aggregate
     bound scales with the worker count -- keeping the stop-the-world frequency
     per worker independent of N. A shared aggregate check (threshold/N) instead
     multiplied the collection count by N and left allocation-heavy parallel
     workloads STW-bound; this keeps them flat. */
  if (SP_GC_CTR_GET(sp_str_heap_bytes_w[wid]) > sp_str_threshold) sp_stw_collect();
  h = (sp_str_hdr *)malloc(total);
  if (!h) sp_oom_die();
  h->size = (uint32_t)total;
  h->len = (uint32_t)len;
  h->hash = 0;
  /* Publish h->next before the head store so a concurrent GC.stat walk that
     observes the new head reaches a fully-linked node (only pushes touch the
     head; the sweep runs under stop-the-world). */
  h->next = sp_str_heap_w[wid];
  sp_str_heap_w[wid] = h;
  SP_GC_CTR_ADD(sp_str_heap_bytes_w[wid], total);
#else
  SP_HEAP_LOCK();
  if (!sp_str_stress_checked) { sp_str_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { SP_GC_CTR_SET(sp_str_threshold, 2048); sp_str_threshold_init = 2048; } }
  if (SP_GC_CTR_GET(sp_str_heap_bytes) > sp_str_threshold) {
    sp_str_collect_retune();         /* sp_gc_collect runs sp_str_sweep */
  }
  h = (sp_str_hdr *)malloc(total);
  if (!h) sp_oom_die();
  h->next = sp_str_heap;
  h->size = (uint32_t)total;
  h->len = (uint32_t)len;
  h->hash = 0;
  sp_str_heap = h;
  SP_GC_CTR_ADD(sp_str_heap_bytes, total);
  SP_HEAP_UNLOCK();
#endif
  /* Don't fold string-heap pressure into sp_gc_bytes: the threshold heuristic
     in sp_gc_alloc is keyed on object-heap survivors, and the str-heap sweep
     that runs alongside (sp_str_sweep, from sp_gc_collect) doesn't add surviving
     strings back into sp_gc_bytes. Folding them in over-fires the object
     heuristic (the reason they're excluded). */
  char *body = (char *)(h + 1);
  body[0] = (char)0xfe;
  body[1 + len] = 0;
  if (sp_alloc_report_on) sp_alloc_report_str(len);
  return body + 1;
}

/* Raw variant: the caller writes a NUL-terminated payload whose final
   length it doesn't know yet (worst-case sized transforms: dump, gsub,
   tr, ...). Leave the header length unset so sp_str_byte_len answers
   strlen until sp_str_set_len records the real (possibly NUL-embedded)
   length; a capacity left in the header would over-read on concat and
   break byte-exact equality. */
#define SP_STR_LEN_UNSET 0xFFFFFFFFu
static inline char *sp_str_alloc_raw(size_t total_with_null) {
  char *s = sp_str_alloc(total_with_null > 0 ? total_with_null - 1 : 0);
  (((sp_str_hdr *)(s - 1)) - 1)->len = SP_STR_LEN_UNSET;
  return s;
}

static inline size_t sp_str_byte_len(const char *s) {
  if (!s) return 0;
  unsigned char marker = ((const unsigned char *)s)[-1];
  /* 0xf1 (frozen heap string / frozen literal) also carries a real sp_str_hdr
     whose len is the true byte length, so an embedded NUL survives freezing
     (#2462 dedup, .freeze). */
  if (marker == 0xfe || marker == 0xfc || marker == 0xfd || marker == 0xf1) {
    uint32_t l = (((const sp_str_hdr *)(s - 1)) - 1)->len;
    if (l != SP_STR_LEN_UNSET) return l;
  }
  return strlen(s);
}

static inline void sp_str_set_len(char *s, size_t len) {
  if (!s) return;
  unsigned char marker = ((unsigned char *)s)[-1];
  if (marker == 0xfe || marker == 0xfc || marker == 0xfd || marker == 0xf1) {
    sp_str_hdr *hd = ((sp_str_hdr *)(s - 1)) - 1;
    hd->len = (uint32_t)len;
    hd->hash = 0;  /* length change implies content change: invalidate cached hash */
  }
}

/* Byte count for the binary-safe return modes (FFI `:binstr`, native binding
   `:cbinstr`). A C function returning bytes rather than a NUL-terminated
   string publishes the exact length here just before it returns; the call site
   reads it instead of calling strlen, which would truncate at an embedded NUL.
   Lives here rather than in any one provider because both sp_net (socket
   reads) and sp_crypto (raw digests) write it. */
extern int sp_ffi_bin_len;

static inline const char *sp_str_from_bytes(const char *data, size_t len) {
  char *s = sp_str_alloc(len);
  if (data) memcpy(s, data, len);
  s[len] = 0;
  return s;
}
static inline const char *sp_str_dup_external(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *r = sp_str_alloc(n);
  memcpy(r, s, n);
  return r;
}

/* Integer / Float -> decimal string. Shared here (over the string heap) so cold
   readers such as lib/sp_json.c can format numbers without sp_runtime.h. */
/* Interpolation writers: append one part into a caller-provided buffer and
   return the new tail. emit_interp sizes the buffer from static bounds
   (SP_W_INT_MAX digits per int, literal lengths) plus strlen of the
   pre-evaluated dynamic parts, so one sp_str_alloc_raw serves the whole
   string (previously: one heap string per part + an sp_sprintf pass). */
#define SP_W_INT_MAX 21  /* -9223372036854775808 */
static inline char *sp_w_int(char *p, mrb_int n) {
  if (n == SP_INT_NIL) return p;  /* a nil int slot interpolates as "" */
  uint64_t u;
  if (n < 0) { *p++ = '-'; u = (uint64_t)(-(n + 1)) + 1; }
  else u = (uint64_t)n;
  char tmp[SP_W_INT_MAX]; int i = 0;
  do { tmp[i++] = (char)('0' + (u % 10)); u /= 10; } while (u > 0);
  while (i > 0) *p++ = tmp[--i];
  return p;
}
/* NOTE: s must be a marked spinel string (heap or codegen literal) --
   sp_str_byte_len reads the marker byte at s[-1], which is out of bounds on
   a foreign C literal. emit_interp therefore uses strlen + memcpy for
   dynamic string parts instead of this helper. */
static inline char *sp_w_str(char *p, const char *s) {
  if (!s) return p;
  size_t l = sp_str_byte_len(s);
  memcpy(p, s, l);
  return p + l;
}
static inline char *sp_w_bool(char *p, mrb_bool v) {
  if (v) { memcpy(p, "true", 4); return p + 4; }
  memcpy(p, "false", 5); return p + 5;
}

static inline const char *sp_int_to_s(mrb_int n){char*b=sp_str_alloc_raw(32);int len=snprintf(b,32,"%lld",(long long)n);if(len<0)len=0;sp_str_set_len(b,(size_t)len);return b;}
/* Float#to_s (Ruby semantics): the shortest decimal that round-trips, fixed
   point for a decimal exponent in [-4, 15], scientific otherwise; NaN, ±Infinity
   and -0.0 match CRuby's spelling. */
/* Float#to_s / #inspect: shortest round-trip decimal. Cold (display only) and
   large, so it lives out-of-line in sp_alloc.c rather than inlining into every
   TU that formats a float. */
const char *sp_float_to_s(mrb_float f);

/* ---- object construction (shared so lib C files can build values) ----
   The built-in cls_id sentinels, the core sp_box_* constructors, the object
   allocator, and sp_PolyArray. Moved here from sp_runtime.h so a standalone TU
   (sp_pack.c, sp_strscan.c, ...) can allocate and box without the per-TU heap
   state. SP_BUILTIN_FOREIGN_PTR/COMPLEX/RATIONAL are in sp_gc.h. */
#define SP_BUILTIN_ARRAY_OF(tag) (-(tag) - 1)
#define SP_BUILTIN_INT_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_INT) /* -1 */
#define SP_BUILTIN_STR_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_STR) /* -2 */
#define SP_BUILTIN_FLT_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_FLT) /* -3 */
#define SP_BUILTIN_PTR_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_OBJ) /* -6 */
#define SP_BUILTIN_SYM_ARRAY SP_BUILTIN_ARRAY_OF(SP_TAG_SYM) /* -7 */
#define SP_BUILTIN_PROC (-9)
#define SP_BUILTIN_RANGE (-10)
#define SP_BUILTIN_TIME (-11)
#define SP_BUILTIN_POLY_ARRAY (-12)
#define SP_BUILTIN_STR_INT_HASH (-13)
#define SP_BUILTIN_STR_STR_HASH (-14)
#define SP_BUILTIN_INT_STR_HASH (-15)
#define SP_BUILTIN_SYM_INT_HASH (-16)
#define SP_BUILTIN_SYM_STR_HASH (-17)
#define SP_BUILTIN_STR_POLY_HASH (-18)
#define SP_BUILTIN_SYM_POLY_HASH (-19)
#define SP_BUILTIN_POLY_POLY_HASH (-20)
#define SP_BUILTIN_OBJECT        (-21)
#define SP_BUILTIN_FIBER         (-22)
#define SP_BUILTIN_IO            (-23)
#define SP_BUILTIN_METHOD        (-24)
#define SP_BUILTIN_ENUMERATOR    (-25)
/* Exception lived at -13, aliasing SP_BUILTIN_STR_INT_HASH, which both put
   exceptions inside the hash cls_id block [-20,-13] (breaking is_a?(Hash) /
   poly .class for exceptions) and made a str_int_hash arriving as a poly value
   misdispatch through the Exception inspect path. Give it a distinct id below
   the hash block so the two no longer collide. */
#define SP_BUILTIN_EXCEPTION     (-28)
#define SP_BUILTIN_THREAD        (-29)
#define SP_BUILTIN_QUEUE         (-30)
#define SP_BUILTIN_MUTEX         (-31)
#define SP_BUILTIN_CONDVAR       (-32)
/* Integer-keyed Integer-valued hash. It sits BELOW the contiguous hash block
   [-20,-13] (which was full), so the "is this a hash" range checks that test
   that block must also test for this id explicitly. */
#define SP_BUILTIN_INT_INT_HASH  (-34)
#define SP_BUILTIN_BASIC_OBJECT  (-37)  /* a bare BasicObject.new instance */
#define SP_BUILTIN_STRBUF        (-40)  /* boxed sp_String* handle: a shared-
                                           mutable string stored in a container
                                           (#3227 phase 3); reads deref the live
                                           buffer, identity is the handle */
#define SP_BUILTIN_DIR           (-38)  /* an open directory handle (sp_Dir *) */
#define SP_BUILTIN_TMS           (-39)  /* Process.times -> Process::Tms */
#define SP_BUILTIN_ADDRINFO      (-40)  /* Addrinfo (sp_Addrinfo *) */
#define SP_BUILTIN_SOCKOPT       (-41)  /* Socket::Option (sp_SockOpt *) */

static inline sp_RbVal sp_box_int(mrb_int v)    { sp_RbVal r; r.tag = SP_TAG_INT;  r.cls_id = 0; r.v.i = v; return r; }
/* A NULL char* IS Ruby nil throughout the string paths (the nullable-string
   invariant); preserve that across the boxing boundary, or a boxed nil-string
   carries SP_TAG_STR and fails tag-keyed comparisons -- `defined?(x).should
   == nil` compared STR(NULL) against NIL and answered false. */
static inline sp_RbVal sp_box_str(const char *v){ sp_RbVal r; if (!v) { r.tag = SP_TAG_NIL; r.cls_id = 0; r.v.s = NULL; return r; } r.tag = SP_TAG_STR;  r.cls_id = 0; r.v.s = v; return r; }
static inline sp_RbVal sp_box_float(mrb_float v){ sp_RbVal r; r.tag = SP_TAG_FLT;  r.cls_id = 0; r.v.f = v; return r; }
/* Write the full union word, not just the narrow `b` member: hash keys and
   poly equality compare bool values through `v.i`, so bytes left
   uninitialized here make two `true`s unequal (a garbage-dependent hash). */
static inline sp_RbVal sp_box_bool(mrb_bool v)  { sp_RbVal r; r.tag = SP_TAG_BOOL; r.cls_id = 0; r.v.i = (v != 0); return r; }
static inline sp_RbVal sp_box_nil(void)         { sp_RbVal r; r.tag = SP_TAG_NIL;  r.cls_id = 0; r.v.i = 0; return r; }
static inline sp_RbVal sp_box_obj(void *p, int cls_id) { sp_RbVal r; r.tag = SP_TAG_OBJ; r.cls_id = cls_id; r.v.p = p; return r; }
static inline sp_RbVal sp_box_sym(sp_sym v)     { if (v == (sp_sym)-1) { sp_RbVal n; n.tag = SP_TAG_NIL; n.cls_id = 0; n.v.i = 0; return n; } sp_RbVal r; r.tag = SP_TAG_SYM;  r.cls_id = 0; r.v.i = (mrb_int)v; return r; }  /* (sp_sym)-1 is the nilable-symbol sentinel: box it as nil, never as :"" */
static inline sp_RbVal sp_box_poly_array(void *p) { return sp_box_obj(p, SP_BUILTIN_POLY_ARRAY); }

/* GC object allocator. The threshold/stress state is extern (defined in
   sp_alloc.c) so every TU shares it -- the same model as sp_gc_heap/bytes.
   sp_gc_alloc itself is an external function (defined in sp_alloc.c) so the
   cold lib C files that already link it (sp_fiber.c, sp_io.c, sp_bigint.c)
   keep resolving the same symbol. */
extern size_t sp_gc_threshold;
extern size_t sp_gc_threshold_init;
extern int sp_gc_stress_checked;
void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *));
void *sp_gc_alloc_nogc(size_t sz, void (*fin)(void *), void (*scn)(void *));

__attribute__((noreturn)) void sp_raise_cls(const char *cls, const char *msg);  /* lib/sp_core.c */
__attribute__((noreturn)) void sp_raise_frozen_str(const char *s);              /* lib/sp_str.c */
/* The message carries the rodata marker byte: an in-flight exception's msg is
   marked by the GC (sp_mark_string reads s[-1]), so a bare literal -- whose
   [-1] is out of bounds -- would be UB when it lands at a section edge. */
static void __attribute__((noinline, cold)) sp_raise_frozen_array(void) { sp_raise_cls("FrozenError", (&("\xff" "can't modify frozen Array")[1])); }
/* Same, but stages the receiver so FrozenError#receiver answers the frozen
   object itself (identity-preserving boxing of the mutation target) (#3002).
   sp_exc_stage_recv lives in the generated TU; the ctor transfers the staged
   value onto the raised exception's xrecv slot. */
void sp_exc_stage_recv(sp_RbVal v);
static void __attribute__((noinline, cold)) sp_raise_frozen_array_at(void *a, int cls_id) {
  sp_exc_stage_recv(sp_box_obj(a, cls_id));
  sp_raise_cls("FrozenError", (&("\xff" "can't modify frozen Array")[1]));
}
/* boxed-receiver variant (the mutator holds an sp_RbVal, not the raw ptr) */
static void __attribute__((noinline, cold)) sp_raise_frozen_array_v(sp_RbVal v) {
  sp_exc_stage_recv(v);
  sp_raise_cls("FrozenError", (&("\xff" "can't modify frozen Array")[1]));
}

/* sp_PolyArray: a growable array of boxed values. */
typedef struct { sp_RbVal *data; mrb_int len; mrb_int cap; mrb_int frozen; } sp_PolyArray;
static inline void sp_PolyArray_scan(void *p) { sp_PolyArray *a = (sp_PolyArray *)p; for (mrb_int i = 0; i < a->len; i++) sp_mark_rbval(a->data[i]); }
static inline void sp_PolyArray_fin(void *p) { sp_PolyArray *a = (sp_PolyArray *)p; sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); sp_gc_bytes_sub(sizeof(sp_RbVal) * a->cap); h->size -= sizeof(sp_RbVal) * a->cap; free(a->data); }
/* Free-list pool for PolyArray, header AND data buffer kept together. The
   allocation-heaviest programs (per-point tuple building: BabyStark's
   constraint evaluation) churn millions of short-lived PolyArrays; recycling
   them turns the calloc+malloc pair and the sweep-side free into list ops.
   Pool state lives in sp_alloc.c; the recycle hook runs inside the sweep. */
extern sp_gc_hdr *sp_polyarr_pool_head;
extern long sp_polyarr_pool_count;
void sp_PolyArray_pool_recycle(sp_gc_hdr *h);
static inline sp_PolyArray *sp_PolyArray_new(void) {
  sp_gc_hdr *ph;
#ifdef SP_THREADS
  do { ph = __atomic_load_n(&sp_polyarr_pool_head, __ATOMIC_ACQUIRE);
  } while (ph && !__atomic_compare_exchange_n(&sp_polyarr_pool_head, &ph, ph->next,
                                              0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
  if (ph) __atomic_fetch_sub(&sp_polyarr_pool_count, 1, __ATOMIC_RELAXED);
#else
  ph = sp_polyarr_pool_head;
  if (ph) { sp_polyarr_pool_head = ph->next; sp_polyarr_pool_count--; }
#endif
  if (ph) {
    /* re-link into the live heap (the sweep unhooked it); size still counts
       header + retained data buffer, so the byte accounting stays exact */
    ph->marked = 0;
    ph->recycle = sp_PolyArray_pool_recycle;
    SP_GC_HEAP_PUSH(ph);
    sp_gc_bytes_add(ph->size);
    sp_PolyArray *a = (sp_PolyArray *)((char *)ph + sizeof(sp_gc_hdr));
    a->len = 0;
    a->frozen = 0;
    return a;
  }
  sp_PolyArray *a = (sp_PolyArray *)sp_gc_alloc(sizeof(sp_PolyArray), sp_PolyArray_fin, sp_PolyArray_scan);
  a->cap = 16; a->data = (sp_RbVal *)malloc(sizeof(sp_RbVal) * a->cap); if (!a->data) sp_oom_die(); a->len = 0;
  { sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); h->recycle = sp_PolyArray_pool_recycle; h->size += sizeof(sp_RbVal) * a->cap; sp_gc_bytes_add(sizeof(sp_RbVal) * a->cap); }
  return a;
}
static inline void sp_PolyArray_push(sp_PolyArray *a, sp_RbVal v) { if (!a) return; if (a->frozen) { sp_raise_frozen_array(); return; } if (a->len >= a->cap) { sp_gc_hdr *h = (sp_gc_hdr *)((char *)a - sizeof(sp_gc_hdr)); sp_gc_bytes_sub(sizeof(sp_RbVal) * a->cap); h->size -= sizeof(sp_RbVal) * a->cap; a->cap = (a->cap * 2) + 1; void *nd = realloc(a->data, sizeof(sp_RbVal) * a->cap); if (!nd) sp_oom_die(); a->data = (sp_RbVal *)nd; h->size += sizeof(sp_RbVal) * a->cap; sp_gc_bytes_add(sizeof(sp_RbVal) * a->cap); } a->data[a->len++] = v; }
static inline sp_RbVal sp_PolyArray_get(sp_PolyArray *a, mrb_int i) { if (!a) return sp_box_nil(); if (i < 0) i += a->len; if (i < 0 || i >= a->len) return sp_box_nil(); return a->data[i]; }
/* ---- relocated from sp_runtime.h: frozen-string check primitives used
   by lib/sp_cold.c's sp_str_setbyte_cow, and the SPL frozen-literal macro
   used by lib/sp_cold.c's sp_gc_stat. Pure textual move (still static
   inline / object-like macro), no codegen change. ---- */
#define SPL(s) (&("\xff" s)[1])
/* 0xf1 = heap string frozen by an explicit .freeze call.
   Unlike 0xff (rodata literal) this marker lives in a malloc'd buffer
   so sp_str_freeze_val can set it.  The frozen? predicate and mutation
   guards check for 0xf1; plain rodata 0xff literals are NOT reported
   as frozen (they behave as immutable value-semantics objects). */
static inline mrb_bool sp_str_is_frozen_val(const char *s) {
  if (!s) return TRUE;
  return ((const unsigned char *)s)[-1] == 0xf1;
}
static inline void sp_str_check_mutable(const char *s) {
  if (sp_str_is_frozen_val(s)) sp_raise_frozen_str(s);
}

/* ---- relocated from sp_runtime.h: integer add/sub/mul overflow-check
   helpers (still static inline, pure textual move) used by lib/sp_cold.c's
   sp_int_pow. ---- */
#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif
#if (defined(__GNUC__) && __GNUC__ >= 5) || \
    (__has_builtin(__builtin_add_overflow) && \
     __has_builtin(__builtin_sub_overflow) && \
     __has_builtin(__builtin_mul_overflow))
#  define SP_HAVE_OVERFLOW_BUILTINS 1
#endif

#ifdef SP_HAVE_OVERFLOW_BUILTINS
static inline mrb_bool sp_int_add_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  return __builtin_add_overflow(a, b, r);
}
static inline mrb_bool sp_int_sub_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  return __builtin_sub_overflow(a, b, r);
}
static inline mrb_bool sp_int_mul_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  return __builtin_mul_overflow(a, b, r);
}
#else
/* Portable fallback for compilers lacking __builtin_*_overflow.
   mrb_int is pointer-width (intptr_t), so compute in uintptr_t --
   unsigned overflow is well-defined wrap-around in C -- and detect
   signed overflow via the sign-bit XOR trick at the *correct* width
   (the sign bit is mrb_int's top bit: 63 on 64-bit, 31 on 32-bit).
   Bounds use INTPTR_MAX/MIN, not the int64 MRB_INT_* macros, so this
   path is self-contained and width-correct. Mul checks bounds before
   multiplying because a 2x-width intermediate isn't portable. */
#define SP_INT_OVF_SIGN ((uintptr_t)1 << (sizeof(mrb_int) * 8 - 1))
static inline mrb_bool sp_int_add_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  uintptr_t x = (uintptr_t)a, y = (uintptr_t)b, z = x + y;
  *r = (mrb_int)z;
  return !!(((x ^ z) & (y ^ z)) & SP_INT_OVF_SIGN);
}
static inline mrb_bool sp_int_sub_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  uintptr_t x = (uintptr_t)a, y = (uintptr_t)b, z = x - y;
  *r = (mrb_int)z;
  return !!(((x ^ z) & (~y ^ z)) & SP_INT_OVF_SIGN);
}
static inline mrb_bool sp_int_mul_overflow_p(mrb_int a, mrb_int b, mrb_int *r) {
  if (a > 0 && b > 0 && a > INTPTR_MAX / b) { *r = a * b; return TRUE; }
  if (a < 0 && b > 0 && a < INTPTR_MIN / b) { *r = a * b; return TRUE; }
  if (a > 0 && b < 0 && b < INTPTR_MIN / a) { *r = a * b; return TRUE; }
  if (a < 0 && b < 0 && (a <= INTPTR_MIN || b <= INTPTR_MIN || -a > INTPTR_MAX / -b)) {
    *r = a * b; return TRUE;
  }
  *r = a * b;
  return FALSE;
}
#undef SP_INT_OVF_SIGN
#endif

/* ---- Integer leaf-op prototypes (bodies relocated to lib/sp_cold.c):
   chr/digits/bit_length/bit_range/to_s_base/opt variants/pow. ---- */
const char *sp_int_chr(mrb_int n);
const char *sp_int_chr_utf8(mrb_int n);
const char *sp_int_codepoint_to_str(mrb_int n);
sp_IntArray *sp_int_digits(mrb_int n, mrb_int base);
mrb_int sp_int_bit_length(mrb_int n);
mrb_int sp_int_bit_range(mrb_int n, mrb_int start, mrb_int len);
const char *sp_int_interp(mrb_int n);
const char *sp_int_to_s_base(mrb_int n, mrb_int base);
const char *sp_int_opt_inspect(mrb_int v);
const char *sp_int_opt_to_s(mrb_int v);
mrb_int sp_int_pow(mrb_int base, mrb_int exp);

/* ---- Float leaf-op prototypes (bodies relocated to lib/sp_cold.c). ---- */
const char *sp_float_opt_inspect(mrb_float v);
const char *sp_float_opt_to_s(mrb_float v);
mrb_int sp_float_denominator(mrb_float f);
sp_RbVal sp_float_numerator(mrb_float f);
mrb_int sp_float_to_i_checked(mrb_float f);

/* ---- forward declarations for pointer-only box params (full types stay
   opaque to lib/sp_alloc.h -- these box functions only store the pointer). ---- */
typedef struct sp_Bigint sp_Bigint;               /* full def: sp_runtime.h bigint block */
typedef struct sp_OpenStruct_s sp_OpenStruct;      /* full def: sp_runtime.h (SymPolyHash-backed) */
typedef struct { mrb_float utime, stime, cutime, cstime; } sp_Tms;
/* Addrinfo: one resolved endpoint. Immutable; the strings are GC-managed. */
typedef struct {
  const char *ip;        /* numeric address, or the path for AF_UNIX */
  const char *afname;    /* "AF_INET" / "AF_INET6" / "AF_UNIX", for #inspect */
  mrb_int afamily;       /* the AF_* value, which is what #afamily answers */
  mrb_int port;
  mrb_int socktype;      /* SOCK_STREAM / SOCK_DGRAM */
  mrb_int protocol;
} sp_Addrinfo;
/* Socket::Option: what #getsockopt answers. Spinel carries the integer-valued
   options only, so the payload is the int itself rather than a byte string. */
typedef struct { mrb_int family, level, optname, value; } sp_SockOpt;

/* ---- Box/Encoding helpers relocated from sp_runtime.h: hot-ish ones
   (sp_box_class 7x / sp_box_nullable_obj 64x / sp_box_int_array 24x /
   sp_box_float_array 12x / sp_box_str_array 5x / sp_box_range 4x in
   optcarrot) stay static inline (pure textual move, no codegen change);
   sp_encoding_name/_inspect/_eq (0 uses) ride along since sp_box_encoding
   needs sp_encoding_name. ---- */
static inline sp_RbVal sp_box_class_name(const char *name) { sp_RbVal r; r.tag = SP_TAG_CLASS; r.cls_id = SP_CLASS_BY_NAME; r.v.s = name; return r; }
/* box a sp_Class into a poly slot (a name-backed class boxes by name). */
static inline sp_RbVal sp_box_class(sp_Class c) { if (sp_class_nil_p(c)) return sp_box_nil(); if (c.name) return sp_box_class_name(c.name); sp_RbVal r; r.tag = SP_TAG_CLASS; r.cls_id = (int)c.cls_id; r.v.i = c.cls_id; return r; }
/* Regexp.escape(s) / Regexp.quote(s) -- prefix every regex metachar
   and whitespace byte with a single backslash, returning a heap
   string that callers can feed into `Regexp.new(...)` to match the
   original bytes literally. Matches CRuby's rb_reg_quote for the
   ASCII range (the only range Spinel's regex engine indexes today,
   so multibyte passes through unchanged).

   The metachars covered: \\ . ? * + ^ $ | ( ) [ ] { } # -
   The whitespace covered: ' ' \t \n \r \f \v
   Everything else copies byte-for-byte. */
static inline sp_RbVal sp_box_nullable_obj(void *p, int cls_id) { return p ? sp_box_obj(p, cls_id) : sp_box_nil(); }
/* Built-in pointer boxes — share SP_TAG_OBJ with a reserved negative
   cls_id so the dispatch path is uniform. */
static inline sp_RbVal sp_box_int_array(void *p)   { return sp_box_obj(p, SP_BUILTIN_INT_ARRAY); }
static inline sp_RbVal sp_box_float_array(void *p) { return sp_box_obj(p, SP_BUILTIN_FLT_ARRAY); }
static inline sp_RbVal sp_box_str_array(void *p)   { return sp_box_obj(p, SP_BUILTIN_STR_ARRAY); }
/* sp_Range is a 16-byte value type that doesn't fit in sp_RbVal's union
   (max 8 bytes). When a Range crosses into a poly slot (heterogeneous
   hash / array / param / ivar), copy it onto the GC heap and box the
   pointer via SP_BUILTIN_RANGE. The Range has no internal pointer fields
   so no scanner is needed. */
static inline sp_RbVal sp_box_range(sp_Range v) {
  sp_Range *p = (sp_Range *)sp_gc_alloc(sizeof(sp_Range), NULL, NULL);
  *p = v;
  return sp_box_obj(p, SP_BUILTIN_RANGE);
}
static inline const char*sp_encoding_name(sp_Encoding e){return e.name?e.name:sp_str_empty;}
static inline const char*sp_encoding_inspect(sp_Encoding e){return sp_sprintf("#<Encoding:%s>",sp_encoding_name(e));}
static inline mrb_bool sp_encoding_eq(sp_Encoding a,sp_Encoding b){const char*an=sp_encoding_name(a);const char*bn=sp_encoding_name(b);return strcmp(an,bn)==0;}

/* ---- Box helper prototypes (0 optcarrot uses; bodies in lib/sp_cold.c). ---- */
sp_RbVal sp_box_int_or_nil(mrb_int v);
sp_RbVal sp_box_bigint(sp_Bigint *b);
sp_RbVal sp_box_encoding(sp_Encoding e);
sp_RbVal sp_box_nullable_str(const char *v);
sp_RbVal sp_box_foreign_ptr(void *p);
sp_RbVal sp_box_regexp(void *p);
sp_RbVal sp_box_sym_array(void *p);
sp_RbVal sp_box_ptr_array(void *p);
sp_RbVal sp_box_method(void *p);
sp_RbVal sp_box_complex(sp_Complex v);
sp_RbVal sp_box_rational(sp_Rational v);
sp_RbVal sp_box_time(sp_Time v);
sp_RbVal sp_box_tms(sp_Tms v);
sp_RbVal sp_box_addrinfo(sp_Addrinfo *v);
sp_RbVal sp_box_sockopt(sp_SockOpt *v);
void sp_addrinfo_scan(void *p);
sp_RbVal sp_box_openstruct(sp_OpenStruct *o);

/* ---- class-frozen bitmap (Class#freeze / #frozen?): state stays
   per-process like sp_argv, extern instead of sp_runtime.h-static. ---- */
extern unsigned char sp_class_frozen_map[4096];   /* one definition: lib/sp_cold.c */
void sp_class_freeze_id(mrb_int cls_id);
mrb_bool sp_class_frozen_id(mrb_int cls_id);

/* ---- rounding helpers relocated from sp_runtime.h (0 optcarrot uses). ---- */
double sp_round_half_even(double x);
double sp_round_half_down(double x);

/* ---- BigRational (Bignum-denominator Rational, #2469): 0 optcarrot
   uses. sp_Bigint is forward-declared above (box_bigint); the specific
   sp_bigint_* ops below are resolved at the final link against the
   generated TU, same as sp_sprintf. ---- */
typedef struct { sp_Bigint *num, *den; } sp_BigRational;
int sp_bigint_sign(sp_Bigint *b);
sp_Bigint *sp_bigint_sub(sp_Bigint *a, sp_Bigint *b);
sp_Bigint *sp_bigint_new_int(int64_t v);
sp_Bigint *sp_bigint_gcd(sp_Bigint *a, sp_Bigint *b);
sp_Bigint *sp_bigint_div(sp_Bigint *a, sp_Bigint *b);
const char *sp_bigint_to_s(sp_Bigint *b);
double sp_bigint_to_double(sp_Bigint *b);
void sp_brat_scan(void *p);
sp_RbVal sp_box_brat(sp_Bigint *num, sp_Bigint *den);
sp_RbVal sp_brat_from_bigint(sp_Bigint *n);
const char *sp_brat_to_s(sp_BigRational *r);
const char *sp_brat_inspect(sp_BigRational *r);
mrb_float sp_brat_to_f(sp_BigRational *r);

/* ---- Marshal.dump/load helpers (lib/sp_marshal.c calls these): 0
   optcarrot uses. sp_marv_hash_new/set stay in sp_runtime.h instead of
   moving here -- they need sp_PolyPolyHash_new/set, which are hot
   (called there dozens of times, e.g. via sp_PolyArray_tally) and
   whose home is the struct's own definition deep in sp_runtime.h, not
   this early header; de-static'ing them in place to reach two
   one-line marv wrappers would grow sp_runtime.h's non-static-body
   count for no real gain, so those two stay put instead. */
sp_RbVal sp_marv_arr_new(void);
void sp_marv_arr_push(sp_RbVal a, sp_RbVal v);
sp_RbVal sp_marv_box_complex(mrb_float re, mrb_float im);
sp_RbVal sp_marv_box_rational(mrb_int n, mrb_int d);
void sp_marv_raise(const char *cls, const char *msg);

/* ---- FFI array data pointers, array-kind length probe, sp_Class
   unboxing: relocated from sp_runtime.h (0 optcarrot uses). ---- */
const int64_t *sp_ffi_int_array_data(sp_RbVal v);
const double *sp_ffi_float_array_data(sp_RbVal v);
const int64_t *sp_PolyArray_ffi_int_data(sp_PolyArray *a);
const double *sp_PolyArray_ffi_float_data(sp_PolyArray *a);
mrb_int sp_array_kind_len(sp_RbVal el);
sp_Class sp_unbox_class(sp_RbVal v);

#endif /* SP_ALLOC_H */
