/* sp_alloc.c -- the single, shared definitions backing sp_alloc.h.

   Owns the string heap so that both the generated program and every standalone
   lib/*.c allocate onto one heap. sp_str_sweep is registered with the object GC
   via a constructor, so a collection triggered from any TU also reaps strings. */
#include "sp_alloc.h"
#include "sp_dtoa.h"   /* sp_format_float for locale-independent Float#to_s */

#ifdef SP_THREADS
sp_str_hdr *sp_str_heap_w[SP_MAX_WORKERS];       /* zero-init: NULL lists */
size_t sp_str_heap_bytes_w[SP_MAX_WORKERS];      /* zero-init: 0 bytes */
/* Aggregate live string bytes across every worker's list. Called only off the
   fast path (collection trigger uses the per-worker slice; sweep/retune here). */
static size_t sp_str_bytes_total(void) {
  size_t s = 0;
  int n = sp_active_workers; if (n < 1) n = 1; if (n > SP_MAX_WORKERS) n = SP_MAX_WORKERS;
  for (int i = 0; i < n; i++) s += SP_GC_CTR_GET(sp_str_heap_bytes_w[i]);
  return s;
}
#else
sp_str_hdr *sp_str_heap = NULL;
size_t sp_str_heap_bytes = 0;
#endif
size_t sp_str_threshold = 256 * 1024;
size_t sp_str_threshold_init = 256 * 1024;
int sp_str_stress_checked = 0;

const char sp_str_empty_data[] = "\xff";

int sp_ffi_bin_len = 0;   /* see sp_alloc.h: byte count for :binstr / :cbinstr */

/* Object-heap collection threshold (was per-TU static in sp_runtime.h; now
   shared so sp_gc_alloc can live in sp_alloc.h and lib TUs allocate too). */
size_t sp_gc_threshold = 256 * 1024;
size_t sp_gc_threshold_init = 256 * 1024;
int sp_gc_stress_checked = 0;

#ifdef SP_THREADS
pthread_mutex_t sp_heap_lock = PTHREAD_MUTEX_INITIALIZER;   /* see sp_alloc.h */

/* One-time SPINEL_GC_STRESS check, run single-threaded before the first helper
   worker spawns (sp_sched_ensure_workers). The alloc fast paths keep their lazy
   `if (!checked)` guard for the single-threaded build, but under threads letting
   workers race to first-write that flag on the hot path is a data race; doing it
   here once means every worker only ever reads it (the pthread_create of the
   helpers is the happens-before edge). Idempotent: safe if main already tripped
   the lazy guard during startup. */
void sp_alloc_stress_init(void) {
  const char *e = getenv("SPINEL_GC_STRESS");
  int stress = (e && *e && *e != '0');
  if (!sp_str_stress_checked) {
    sp_str_stress_checked = 1;
    if (stress) { sp_str_threshold = 2048; sp_str_threshold_init = 2048; }
  }
  if (!sp_gc_stress_checked) {
    sp_gc_stress_checked = 1;
    if (stress) { SP_GC_CTR_SET(sp_gc_threshold, 2048); sp_gc_threshold_init = 2048; }
  }
}
#endif

/* Re-tune the object / string GC thresholds from the pre-collect live bytes
   (the heuristic mirrors the original inline code in sp_gc_alloc / sp_str_alloc). */
void sp_gc_retune_object(size_t before) {
  size_t freed = before - sp_gc_bytes;
  if (freed < before / 4) { sp_gc_threshold = before * 2; }
  else if (sp_gc_bytes > 0) { sp_gc_threshold = sp_gc_bytes * 4; if (sp_gc_threshold < sp_gc_threshold_init) sp_gc_threshold = sp_gc_threshold_init; }
  else { sp_gc_threshold = sp_gc_threshold_init; }
}
/* `before` is the pre-sweep live bytes; `after` the survivors. The threshold is
   the PER-WORKER budget (each worker triggers on its own list, so the aggregate
   heap is bounded by N * threshold). Retune on the per-worker average so the
   budget tracks a single worker's survivor size and does NOT inflate by N each
   cycle -- retuning on the aggregate would grow it geometrically for long-lived
   strings. The single-threaded build works in absolute bytes (N == 1). */
static void sp_str_retune(size_t before) {
#ifdef SP_THREADS
  int nw = sp_active_workers; if (nw < 1) nw = 1;
  size_t after = sp_str_bytes_total() / (size_t)nw;
  before /= (size_t)nw;
#else
  size_t after = sp_str_heap_bytes;
#endif
  size_t freed = before - after;
  if (freed < before / 4) { sp_str_threshold = before * 2; }
  else if (after > 0) { sp_str_threshold = after * 4; if (sp_str_threshold < sp_str_threshold_init) sp_str_threshold = sp_str_threshold_init; }
  else { sp_str_threshold = sp_str_threshold_init; }
}

/* Collect and re-tune. The caller guarantees exclusive heap access: the
   single-threaded allocators hold sp_heap_lock; the threaded build runs the
   _all variant under stop-the-world (every other worker parked), via
   sp_stw_collect, so neither heap is mutated during the sweep. The object and
   string variants retune only their own threshold, matching the original
   per-heap inline collection so the single-threaded path stays byte-identical;
   _all retunes both since one stop-the-world sweeps both heaps. */
void sp_gc_collect_retune(void) {
  /* the retune hook inside sp_gc_collect adjusts the object threshold */
  sp_gc_collect();
  sp_gc_enforce_mem_limit();
}
void sp_str_collect_retune(void) {
  /* the gated sweep hook inside sp_gc_collect retunes the string threshold */
  sp_gc_collect();
}
void sp_gc_collect_retune_all(void) {
  sp_gc_collect();
  sp_gc_enforce_mem_limit();
}
/* Either heap over its trigger? Used by sp_stw_collect to skip a redundant
   stop-the-world when another worker just collected. */
int sp_gc_collection_wanted(void) {
  /* Everything read atomically: this runs before the world is stopped
     (sp_stw_collect's early-out), concurrent with other workers' relaxed
     counter adds AND with the allocators' one-shot GC-stress threshold
     write (heap-locked, but this reader holds only the sched lock). The
     retune writes are plain but never overlap: they run while g_stw_active
     is set, and this is only called with it clear, under the same lock
     that publishes it. A stale read at worst skips one redundant
     collection. */
#ifdef SP_THREADS
  return SP_GC_CTR_GET(sp_gc_bytes) > SP_GC_CTR_GET(sp_gc_threshold) ||
         sp_str_bytes_total() > SP_GC_CTR_GET(sp_str_threshold);
#else
  return SP_GC_CTR_GET(sp_gc_bytes) > SP_GC_CTR_GET(sp_gc_threshold) ||
         SP_GC_CTR_GET(sp_str_heap_bytes) > SP_GC_CTR_GET(sp_str_threshold);
#endif
}

void *sp_gc_alloc(size_t sz, void (*fin)(void *), void (*scn)(void *)) {
#ifdef SP_THREADS
  /* Lock-free fast path: the list push is a CAS (SP_GC_HEAP_PUSH) and the live-
     byte counter is atomic, so concurrent allocations need no mutex -- the old
     sp_heap_lock only serialized them and the string sweep, and both string
     allocation (per-worker heap) and every collection (stop-the-world) have
     moved off it. Removals happen only under stop-the-world with every mutator
     parked, so a push never races the sweep. The stress-threshold one-shot is
     idempotent under a race. */
  if (!sp_gc_stress_checked) { sp_gc_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { SP_GC_CTR_SET(sp_gc_threshold, 2048); sp_gc_threshold_init = 2048; } }
  if (SP_GC_CTR_GET(sp_gc_bytes) > SP_GC_CTR_GET(sp_gc_threshold)) sp_stw_collect();
  size_t need = sizeof(sp_gc_hdr) + sz;
  sp_gc_hdr *h = (sp_gc_hdr *)calloc(1, need);
  if (!h) sp_oom_die();
  h->finalize = fin; h->scan = scn; h->size = need; h->marked = 0;
  if (sp_alloc_report_on) sp_alloc_report_count((void *)scn, sz);
  SP_GC_HEAP_PUSH(h); sp_gc_bytes_add(need);
  return (char *)h + sizeof(sp_gc_hdr);
#else
  SP_HEAP_LOCK();
  /* The threshold store is atomic: sp_gc_collection_wanted reads it without
     the heap lock. threshold_init stays plain -- only retune reads it, under
     stop-the-world, ordered after this by the writer's park. */
  if (!sp_gc_stress_checked) { sp_gc_stress_checked = 1; const char *e = getenv("SPINEL_GC_STRESS"); if (e && *e && *e != '0') { SP_GC_CTR_SET(sp_gc_threshold, 2048); sp_gc_threshold_init = 2048; } }
  if (SP_GC_CTR_GET(sp_gc_bytes) > sp_gc_threshold) {
    sp_gc_collect_retune();
  }
  size_t need = sizeof(sp_gc_hdr) + sz;
  sp_gc_hdr *h = (sp_gc_hdr *)calloc(1, need);
  if (!h) sp_oom_die();
  h->finalize = fin; h->scan = scn; h->size = need; h->marked = 0;
  if (sp_alloc_report_on) sp_alloc_report_count((void *)scn, sz);
  SP_GC_HEAP_PUSH(h); sp_gc_bytes_add(need);
  SP_HEAP_UNLOCK();
  return (char *)h + sizeof(sp_gc_hdr);
#endif
}
void *sp_gc_alloc_nogc(size_t sz, void (*fin)(void *), void (*scn)(void *)) {
  size_t need = sizeof(sp_gc_hdr) + sz;
  sp_gc_hdr *h = (sp_gc_hdr *)calloc(1, need);
  if (!h) sp_oom_die();
  h->finalize = fin; h->scan = scn; h->size = need; h->marked = 0;
  if (sp_alloc_report_on) sp_alloc_report_count((void *)scn, sz);
  SP_HEAP_LOCK();
  SP_GC_HEAP_PUSH(h); sp_gc_bytes_add(need);
  SP_HEAP_UNLOCK();
  return (char *)h + sizeof(sp_gc_hdr);
}

SP_TLS struct sp_str_lcache_entry sp_str_lcache[SP_STR_LCACHE_SIZE];
SP_TLS void *_sp_ret_strbuf;

void sp_str_lcache_clear(void) {
  for (unsigned i = 0; i < SP_STR_LCACHE_SIZE; i++) sp_str_lcache[i].s = NULL;
}

/* sp_mark_string (sp_gc.h) flips a live string's marker 0xfe->0xfc during the
   mark phase; sweep keeps the marked ones and frees the rest. A frozen heap
   string (0xf1) is kept across sweeps (a live frozen global must survive, and
   frozen literals are immortal). */
/* Sweep one worker's list head (or the single st list). Runs under stop-the-
   world (threaded) or the held heap lock (st), so no concurrent push races it.
   `bytes` is decremented per freed string to keep the live-byte count in step. */
static void sp_str_sweep_list(sp_str_hdr **head, size_t *bytes) {
  sp_str_hdr **pp = head;
  while (*pp) {
    sp_str_hdr *h = *pp;
    char *body = (char *)(h + 1);
    if ((unsigned char)body[0] == 0xfc) {
      body[0] = (char)0xfe;
      pp = &h->next;
    }
    else if ((unsigned char)body[0] == 0xf1) {
      pp = &h->next;
    }
    else {
      *pp = h->next;
      *bytes -= h->size;
      free(h);
    }
  }
}

void sp_str_sweep(void) {
#ifdef SP_THREADS
  int n = sp_active_workers; if (n < 1) n = 1; if (n > SP_MAX_WORKERS) n = SP_MAX_WORKERS;
  for (int i = 0; i < n; i++) sp_str_sweep_list(&sp_str_heap_w[i], &sp_str_heap_bytes_w[i]);
#else
  sp_str_sweep_list(&sp_str_heap, &sp_str_heap_bytes);
#endif
  sp_str_lcache_clear();
}

/* PolyArray free-list pool (see sp_alloc.h). Bounded so a burst does not pin
   memory forever; an over-cap or oversized-buffer entry frees normally. The
   scan/finalize hooks stay valid on recycled headers -- only `next` and the
   heap-byte accounting change hands. */
sp_gc_hdr *sp_polyarr_pool_head = NULL;
long sp_polyarr_pool_count = 0;
#define SP_POLYARR_POOL_MAX 65536
#define SP_POLYARR_POOL_KEEP_CAP 64   /* don't retain unusually large buffers */
void sp_PolyArray_pool_recycle(sp_gc_hdr *h) {
  sp_PolyArray *a = (sp_PolyArray *)((char *)h + sizeof(sp_gc_hdr));
  long n;
#ifdef SP_THREADS
  n = __atomic_load_n(&sp_polyarr_pool_count, __ATOMIC_RELAXED);
#else
  n = sp_polyarr_pool_count;
#endif
  if (n >= SP_POLYARR_POOL_MAX || a->cap > SP_POLYARR_POOL_KEEP_CAP) {
    free(a->data);
    free(h);
    return;
  }
#ifdef SP_THREADS
  sp_gc_hdr *old;
  do { old = __atomic_load_n(&sp_polyarr_pool_head, __ATOMIC_ACQUIRE); h->next = old;
  } while (!__atomic_compare_exchange_n(&sp_polyarr_pool_head, &old, h,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
  __atomic_fetch_add(&sp_polyarr_pool_count, 1, __ATOMIC_RELAXED);
#else
  h->next = sp_polyarr_pool_head;
  sp_polyarr_pool_head = h;
  sp_polyarr_pool_count++;
#endif
}

/* String sweep, gated on the string heap's own trigger. The object collector
   used to run the full live-string walk on EVERY collection, making each one
   O(live strings) -- the dominant cost of allocation-heavy programs (#2922
   profiling on BabyStark: 2.9s of an 8.0s GC total). Skipping is safe:
   string marks accumulate, so a dead string at worst survives until the next
   string sweep (delayed reclamation, not a leak); the sweep itself resets
   marks for the next cycle. Retuning here (with the collector-side retunes
   removed) keeps the trigger tracking the live size in one place. */
static void sp_str_sweep_gated(void) {
#ifdef SP_THREADS
  size_t before = sp_str_bytes_total();
#else
  size_t before = SP_GC_CTR_GET(sp_str_heap_bytes);
#endif
  if (before <= SP_GC_CTR_GET(sp_str_threshold)) return;
  sp_str_sweep();
  sp_str_retune(before);
}

/* Wire string sweep into the object collector. Runs before main, so the hook is
   set before the first allocation can trigger a collection. */
__attribute__((constructor)) static void sp_alloc_install_hooks(void) {
  sp_gc_str_sweep_hook = sp_str_sweep_gated;
  sp_gc_obj_retune_hook = sp_gc_retune_object;
}

/* Float#to_s / #inspect (declared in sp_alloc.h): shortest round-trip decimal.
   sp_float_shortest gives the shortest significant digits + decimal exponent
   with no locale dependency (pure integer arithmetic; see sp_dtoa.c); the
   fixed vs scientific layout is Ruby's Float#to_s rule (which differs from
   %g's), preserved from the previous strtod-probe implementation. */
const char *sp_float_to_s(mrb_float f) {
  if(f!=f){char*r=sp_str_alloc_raw(4);r[0]='N';r[1]='a';r[2]='N';r[3]=0;return r;}
  if(f==HUGE_VAL||f==-HUGE_VAL){if(f<0){char*r=sp_str_alloc_raw(10);memcpy(r,"-Infinity",10);return r;}char*r=sp_str_alloc_raw(9);memcpy(r,"Infinity",9);return r;}
  if(f==0.0){if(signbit(f)){char*r=sp_str_alloc_raw(5);memcpy(r,"-0.0",5);return r;}char*r=sp_str_alloc_raw(4);memcpy(r,"0.0",4);return r;}
  int neg = signbit(f);
  char digits[32]; int dlen;
  int exp = sp_float_shortest(neg ? -f : f, digits, &dlen);
  int decpt = exp + 1;   /* number of digits before the decimal point in fixed form */
  char *out=sp_str_alloc_raw(64);int o=0;
  if(neg)out[o++]='-';
  /* fixed notation when the point sits within the digits (a fractional part,
     dlen>decpt) OR the integer part is <= 15 digits; a longer integer-valued
     value (dlen<=decpt, decpt>15) prints scientific like CRuby (#2593). */
  if(decpt>0&&(decpt<=15||dlen>decpt)){
    if(decpt<dlen){memcpy(out+o,digits,decpt);o+=decpt;out[o++]='.';memcpy(out+o,digits+decpt,dlen-decpt);o+=(dlen-decpt);}
    else{memcpy(out+o,digits,dlen);o+=dlen;for(int i=dlen;i<decpt;i++)out[o++]='0';out[o++]='.';out[o++]='0';}
  }else if(decpt<=0&&decpt>-4){
    out[o++]='0';out[o++]='.';for(int i=decpt;i<0;i++)out[o++]='0';memcpy(out+o,digits,dlen);o+=dlen;
  }else{
    out[o++]=digits[0];out[o++]='.';
    if(dlen==1)out[o++]='0';else{memcpy(out+o,digits+1,dlen-1);o+=(dlen-1);}
    out[o++]='e';int e=decpt-1;
    if(e>=0)out[o++]='+';else{out[o++]='-';e=-e;}
    if(e<10){out[o++]='0';out[o++]=(char)('0'+e);}else o+=snprintf(out+o,16,"%d",e);
  }
  out[o]=0;sp_str_set_len(out,(size_t)o);return out;
}

/* ---- SPINEL_ALLOC_REPORT: deterministic allocation counters (#1336) ----
   Env-var gated (set to 1 or an output path); zero work when off beyond one
   predictable branch at each allocation entry point. Counters key on the
   object's scan callback (the de-facto type identity); sp_alloc_report_tag
   attaches human names (builtins + user classes, registered by the generated
   prologue when the gate is on). Strings count separately (no scan fn).
   Dump: folded `alloc;<Type> <count>` lines plus `# bytes` comments, to the
   env value as a path, or stderr when it is "1". No signals, no allocation
   in the hot path, portable (plain counters + atexit). */
int sp_alloc_report_on = 0;
typedef struct { void *key; const char *name; unsigned long long count, bytes; } sp_AllocStat;
#define SP_ALLOC_STATS 512
static sp_AllocStat sp_alloc_stats[SP_ALLOC_STATS];
static unsigned long long sp_alloc_str_count, sp_alloc_str_bytes;

static sp_AllocStat *sp_alloc_stat_slot(void *key) {
  size_t h = ((size_t)(uintptr_t)key >> 4) % SP_ALLOC_STATS;
  for (size_t i = 0; i < SP_ALLOC_STATS; i++) {
    sp_AllocStat *s = &sp_alloc_stats[(h + i) % SP_ALLOC_STATS];
    if (s->key == key || s->key == NULL) { s->key = key; return s; }
  }
  return &sp_alloc_stats[h];   /* table full: merge into the home slot */
}
static unsigned long long sp_alloc_noscan_count, sp_alloc_noscan_bytes;
void sp_alloc_report_count(void *scan, size_t bytes) {
  if (!scan) {   /* NULL is the table's empty marker: dedicated bucket */
    sp_alloc_noscan_count++; sp_alloc_noscan_bytes += (unsigned long long)bytes;
    return;
  }
  sp_AllocStat *s = sp_alloc_stat_slot(scan);
  s->count++; s->bytes += (unsigned long long)bytes;
}
void sp_alloc_report_str(size_t bytes) {
  sp_alloc_str_count++; sp_alloc_str_bytes += (unsigned long long)bytes;
}
void sp_alloc_report_tag(void *scan, const char *name) {
  sp_alloc_stat_slot(scan)->name = name;
}
static void sp_alloc_report_dump(void) {
  const char *out = getenv("SPINEL_ALLOC_REPORT");
  FILE *f = stderr;
  int close_f = 0;
  if (out && out[0] && strcmp(out, "1") != 0) {
    FILE *g = fopen(out, "w");
    if (g) { f = g; close_f = 1; }
  }
  if (sp_alloc_str_count) fprintf(f, "alloc;String %llu\n", sp_alloc_str_count);
  if (sp_alloc_noscan_count) fprintf(f, "alloc;(no-scan) %llu\n", sp_alloc_noscan_count);
  for (size_t i = 0; i < SP_ALLOC_STATS; i++) {
    sp_AllocStat *s = &sp_alloc_stats[i];
    if (!s->key || !s->count) continue;
    if (s->name) fprintf(f, "alloc;%s %llu\n", s->name, s->count);
    else fprintf(f, "alloc;scan_%p %llu\n", s->key, s->count);
  }
  if (sp_alloc_str_count) fprintf(f, "# bytes String %llu\n", sp_alloc_str_bytes);
  if (sp_alloc_noscan_count) fprintf(f, "# bytes (no-scan) %llu\n", sp_alloc_noscan_bytes);
  for (size_t i = 0; i < SP_ALLOC_STATS; i++) {
    sp_AllocStat *s = &sp_alloc_stats[i];
    if (!s->key || !s->count) continue;
    if (s->name) fprintf(f, "# bytes %s %llu\n", s->name, s->bytes);
    else fprintf(f, "# bytes scan_%p %llu\n", s->key, s->bytes);
  }
  if (close_f) fclose(f);
}
__attribute__((constructor)) static void sp_alloc_report_boot(void) {
  const char *e = getenv("SPINEL_ALLOC_REPORT");
  if (e && *e && strcmp(e, "0") != 0) {
    sp_alloc_report_on = 1;
    atexit(sp_alloc_report_dump);
  }
}
