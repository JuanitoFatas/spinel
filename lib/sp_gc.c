/* sp_gc.c -- the mark/sweep collector's non-inline machinery.
 * See sp_gc.h. The program root-marking and string-heap sweep are
 * supplied by the generated TU via sp_gc_mark_globals_hook /
 * sp_gc_str_sweep_hook. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if defined(__GLIBC__)
#include <malloc.h>
#else
/* Darwin's libc has no malloc_trim; make it a no-op so call sites stay portable. */
#define malloc_trim(x) ((void)0)
#endif
#include <unistd.h>
#include "sp_gc.h"
#include <signal.h>
#include <unistd.h>
#include "sp_marshal.h"   /* sp_marshal_vt -- the instance lives here (always linked) */

/* ---- Globals shared with the generated TU (declared extern in sp_gc.h) ---- */
SP_TLS void **sp_gc_roots[SP_GC_STACK_MAX];   /* per-worker (SP_TLS); see sp_gc.h */
SP_TLS int sp_gc_nroots = 0;
#ifdef SP_THREADS
sp_gc_wslot_t sp_gc_wslot[SP_MAX_WORKERS];   /* per-worker young head + flush delta, cache-line padded */
#else
sp_gc_hdr *sp_gc_heap = NULL;
#endif
size_t sp_gc_bytes = 0;
size_t sp_gc_old_bytes = 0;
int sp_gc_cycle = 0;
void (*sp_gc_mark_suspended_fibers_hook)(void) = NULL;
void (*sp_gc_mark_globals_hook)(void) = NULL;
void (*sp_gc_str_sweep_hook)(void) = NULL;
const char *(*sp_sym_name_fn)(sp_sym) = NULL;
int (*sp_json_kind_fn)(sp_RbVal) = NULL;
mrb_int (*sp_json_len_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_json_aref_fn)(sp_RbVal, mrb_int) = NULL;
void (*sp_json_hpair_fn)(sp_RbVal, mrb_int, sp_RbVal *, sp_RbVal *) = NULL;
sp_RbVal (*sp_json_mk_hash_fn)(void) = NULL;
sp_sym (*sp_json_sym_intern_fn)(const char *) = NULL;
void (*sp_json_hash_set_fn)(sp_RbVal, const char *, sp_RbVal) = NULL;
const char *(*sp_poly_inspect_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_hash_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_h_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_to_a_fn)(sp_RbVal) = NULL;
sp_RbVal (*sp_obj_with_fn)(sp_RbVal, sp_RbVal) = NULL;
const char *(*sp_obj_inspect_fn)(int cls_id, void *p) = NULL;
const char *(*sp_obj_to_s_fn)(int cls_id, void *p) = NULL;
sp_marshal_vt sp_marshal_v = {0};   /* filled by the generated TU (sp_re_init) */

/* ---- Collector-private globals ---- */
static int sp_gc_verify = 0;
static sp_gc_hdr *sp_gc_old_heap = NULL;
#define SP_GC_MARK_STACK_MAX (1024*64)
static void **sp_gc_mark_stack = NULL;
static int sp_gc_mark_top = 0;
static sp_gc_hdr **sp_gc_vsnap = NULL;
static size_t sp_gc_vsnap_n = 0, sp_gc_vsnap_cap = 0;
static size_t sp_gc_max_bytes = 0;
static int sp_gc_max_bytes_init = 0;
#define SP_GC_FULL_INTERVAL 8

/* Issue #755: bail out cleanly on OOM rather than returning NULL into a
   caller that would deref it next. */
void sp_oom_die(void){fputs("unhandled exception: out of memory\n",stderr);exit(1);}

/* ---- GC verify (SPINEL_GC_VERIFY=1): a sorted snapshot of every
 * registered header, so the scan-time membership test is O(log n). ---- */
static int sp_gc_vsnap_cmp(const void *a, const void *b){ uintptr_t x=(uintptr_t)*(sp_gc_hdr*const*)a, y=(uintptr_t)*(sp_gc_hdr*const*)b; return x<y?-1:x>y?1:0; }
static void sp_gc_vsnap_push(sp_gc_hdr *h){ if(sp_gc_vsnap_n==sp_gc_vsnap_cap){ size_t c=sp_gc_vsnap_cap?sp_gc_vsnap_cap*2:1024; sp_gc_hdr**n=(sp_gc_hdr**)realloc(sp_gc_vsnap,c*sizeof(sp_gc_hdr*)); if(!n)sp_oom_die(); sp_gc_vsnap=n; sp_gc_vsnap_cap=c; } sp_gc_vsnap[sp_gc_vsnap_n++]=h; }
static void sp_gc_verify_snapshot(void){ sp_gc_vsnap_n=0;
#ifdef SP_THREADS
  { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS; for(int i=0;i<n;i++)for(sp_gc_hdr*p=sp_gc_wslot[i].young;p;p=p->next)sp_gc_vsnap_push(p); }
#else
  for(sp_gc_hdr*p=sp_gc_heap;p;p=p->next)sp_gc_vsnap_push(p);
#endif
  for(sp_gc_hdr*p=sp_gc_old_heap;p;p=p->next)sp_gc_vsnap_push(p); if(sp_gc_vsnap_n>1)qsort(sp_gc_vsnap,sp_gc_vsnap_n,sizeof(sp_gc_hdr*),sp_gc_vsnap_cmp); }
static int sp_gc_obj_registered(sp_gc_hdr *h){ uintptr_t hv=(uintptr_t)h; size_t lo=0,hi=sp_gc_vsnap_n; while(lo<hi){ size_t m=lo+(hi-lo)/2; uintptr_t x=(uintptr_t)sp_gc_vsnap[m]; if(x==hv)return 1; if(x<hv)lo=m+1; else hi=m; } return 0; }
/* Verify diagnostics: which phase/slot the bad pointer came from. */
int sp_gc_verify_on(void) { return sp_gc_verify; }
const char *sp_gc_dbg_phase = "?";
void *sp_gc_dbg_ctx = NULL;
static void sp_gc_verify_fail(void *obj, sp_gc_hdr *h){
  fprintf(stderr, "  [phase=%s ctx=%p]\n", sp_gc_dbg_phase, sp_gc_dbg_ctx);
  fprintf(stderr,
    "\n*** SPINEL_GC_VERIFY: collector reached a non-heap/corrupt object ***\n"
    "  obj    = %p\n  header = %p\n"
    "  This pointer is on the GC mark path but is not a registered live GC\n"
    "  allocation -- most likely a raw/aliased pointer (e.g. into a string or\n"
    "  builder buffer) reachable from a root or a scanned field. Invoking its\n"
    "  scan hook would jump through a bogus function pointer.\n",
    obj, (void*)h);
  fflush(stderr);
  fprintf(stderr, "  ->scan = %p   ->size = %zu\n\n",
    (void*)(uintptr_t)h->scan, (size_t)h->size);
  abort();
}
/* Under verify, a fault ON the mark path is the interesting case and the one
   the existing check cannot reach: sp_gc_mark reads the marker byte BEFORE it
   can ask whether the object is registered, so a pointer whose [-1] is
   unreadable (a bare literal at the start of a rodata page, an interior
   pointer into an unmapped neighbour) takes the process down with nothing said.
   Name the root slot the collector was walking, which is what turns an
   unreproducible crash into a variable. Re-raises so the core dump is still
   produced. */
static void sp_gc_fault_report(int sig) {
  static const char hex[] = "0123456789abcdef";
  char buf[256]; size_t o = 0;
  const char *m1 = "\n*** SPINEL_GC_VERIFY: fault on the GC mark path (signal ";
  for (const char *p = m1; *p; p++) buf[o++] = *p;
  buf[o++] = (char)('0' + (sig / 10) % 10); buf[o++] = (char)('0' + sig % 10);
  const char *m2 = ")\n  phase = ";
  for (const char *p = m2; *p; p++) buf[o++] = *p;
  for (const char *p = sp_gc_dbg_phase ? sp_gc_dbg_phase : "?"; *p && o < 200; p++) buf[o++] = *p;
  const char *m3 = "\n  root slot = 0x";
  for (const char *p = m3; *p; p++) buf[o++] = *p;
  uintptr_t v = (uintptr_t)sp_gc_dbg_ctx;
  for (int i = (int)(sizeof(v) * 2) - 1; i >= 0; i--) buf[o++] = hex[(v >> (i * 4)) & 0xf];
  const char *m4 = "\n  The slot's value is the pointer the collector could not read.\n";
  for (const char *p = m4; *p; p++) buf[o++] = *p;
  ssize_t wr = write(2, buf, o); (void)wr;
  signal(sig, SIG_DFL);
  raise(sig);
}
__attribute__((constructor)) static void sp_gc_debug_env(void){
  const char *v=getenv("SPINEL_GC_VERIFY"); sp_gc_verify=(v&&*v&&*v!='0');
  { const char *g=getenv("SPINEL_GC_VERIFY_GEN"); sp_gc_verify_gen=(g&&*g&&*g!='0');
    const char *mn=getenv("SPINEL_GC_MINOR"); sp_gc_minor_on=(mn&&*mn&&*mn!='0');
    if(sp_gc_verify_gen) sp_gc_minor_on=1; }
  if (sp_gc_verify) { signal(SIGSEGV, sp_gc_fault_report); signal(SIGBUS, sp_gc_fault_report); }
}

/* Tag byte preceding `obj`: 0xfe heap-unmarked -> 0xfc; 0xfc/0xff/0xfd/0xf1
 * skipped; else a real GC object reached through its scan hook. */
void sp_gc_mark(void*obj){if(!obj)return;unsigned char pm=((unsigned char*)obj)[-1];if(pm==0xfe){((char*)obj)[-1]=(char)0xfc;return;}if(pm==0xfc||pm==0xff||pm==0xfd||pm==0xf1)return;sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));if(sp_gc_verify&&!sp_gc_obj_registered(h))sp_gc_verify_fail(obj,h);if(sp_gc_verify_probe_on){if(!h->old&&h->marked==sp_gc_verify_probe)sp_gc_verify_probe_hit=1;return;}if(h->marked==sp_gc_mark_gen)return;if(sp_gc_minor&&h->old)return;h->marked=sp_gc_mark_gen;if(h->scan){if(sp_gc_mark_stack&&sp_gc_mark_top<SP_GC_MARK_STACK_MAX){sp_gc_mark_stack[sp_gc_mark_top++]=obj;}
else{h->scan(obj);}}}

void sp_gc_mark_drain(void){
  while(sp_gc_mark_top>0){void*obj=sp_gc_mark_stack[--sp_gc_mark_top];
    sp_gc_hdr*h=(sp_gc_hdr*)((char*)obj-sizeof(sp_gc_hdr));if(h->scan)h->scan(obj);}
}
void sp_gc_mark_all(void){if(!sp_gc_mark_stack)sp_gc_mark_stack=(void**)malloc(sizeof(void*)*SP_GC_MARK_STACK_MAX);sp_gc_mark_top=0;if(sp_gc_verify)sp_gc_verify_snapshot();int vd=sp_gc_verify;for(int i=0;i<sp_gc_nroots;i++){void**e=sp_gc_roots[i];if(vd){sp_gc_dbg_phase="root";sp_gc_dbg_ctx=(void*)e;}if((uintptr_t)e&(uintptr_t)3){sp_gc_mark_root_entry(e);}
else{void*obj=*e;if(obj)sp_gc_mark(obj);}}if(vd)sp_gc_dbg_phase="fibers";if(sp_gc_mark_suspended_fibers_hook)sp_gc_mark_suspended_fibers_hook();if(vd)sp_gc_dbg_phase="globals";if(sp_gc_mark_globals_hook)sp_gc_mark_globals_hook();sp_gc_mark_drain();if(vd){sp_gc_dbg_phase="?";sp_gc_dbg_ctx=NULL;}}

unsigned sp_gc_mark_gen = 0;
/* Set for the duration of a minor mark: an object already promoted is not
   walked, because the sweep does not free the old list on a minor cycle and
   the remembered set carries the old->young references the walk would miss. */
int sp_gc_minor = 0;
int sp_gc_minor_on = 0;
int sp_gc_verify_gen = 0;
int sp_gc_verify_gen_fail = 0;
int sp_gc_verify_probe_on = 0, sp_gc_verify_probe_hit = 0;
unsigned sp_gc_verify_probe = 0;
void *sp_gc_remembered[SP_GC_REMEMBERED_MAX];
int sp_gc_nremembered = 0;
int sp_gc_rem_overflow = 0;
/* Object-threshold retune, installed by sp_alloc.c. Running it INSIDE every
   collection (not only on the object-triggered wrapper) keeps the trigger
   tracking the live size whichever heap initiated the collect; the old
   split retunes left one threshold stale and re-triggered immediately. */
void (*sp_gc_obj_retune_hook)(size_t before) = NULL;
/* Minor sweep of one young list (under stop-the-world): free/recycle the dead,
   promote survivors into the shared old heap, accumulating survivor bytes into
   sp_gc_bytes and sp_gc_old_bytes (both pre-seeded by the caller). */
static void sp_gc_sweep_young(sp_gc_hdr **pp){
  while(*pp){sp_gc_hdr*h=*pp;if(h->marked!=sp_gc_mark_gen){*pp=h->next;if(h->recycle){h->recycle(h);}
  else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));free(h);}}
  else{*pp=h->next;h->next=sp_gc_old_heap;sp_gc_old_heap=h;h->old=1;sp_gc_old_bytes+=h->size;sp_gc_bytes+=h->size;}}
}
#ifdef SP_THREADS
/* One worker's young list, swept BY THAT WORKER while it is parked at the
   stop-the-world barrier. Two things make this worth the barrier phase it
   needs. It is parallel -- the sweep was 93% of the stopped time and every
   other worker idled through it. And the frees go back to the arena the
   allocation came from: a single collector thread freeing eight workers'
   objects takes eight different arena locks and touches eight cold sets of
   metadata, which is why the serial sweep got MORE expensive as workers were
   added (51 ms at one worker, 188 ms at eight, for the same allocations).

   Survivors are collected into a caller-owned local list rather than pushed
   straight onto the shared old heap: that is the one part that cannot be
   concurrent, so it becomes an O(workers) splice the collector does after. */
void sp_gc_sweep_slot(int wid, sp_gc_hdr **out_head, sp_gc_hdr **out_tail, size_t *out_bytes) {
  sp_gc_hdr **pp = &sp_gc_wslot[wid].young;
  sp_gc_hdr *head = NULL, *tail = NULL;
  size_t live = 0;
  while (*pp) {
    sp_gc_hdr *h = *pp;
    *pp = h->next;
    if (h->marked != sp_gc_mark_gen) {
      if (h->recycle) { h->recycle(h); }
      else { if (h->finalize) h->finalize((char *)h + sizeof(sp_gc_hdr)); free(h); }
    }
    else {
      h->next = head; head = h;
      if (!tail) tail = h;
      h->old = 1;                 /* survivor: joins the old list (see sp_gc_wb) */
      live += h->size;
    }
  }
  *out_head = head; *out_tail = tail; *out_bytes = live;
}
/* Installed by the scheduler when it can drive the parked workers; NULL means
   nobody is parked to help and the collector sweeps every slot itself. */
void (*sp_gc_par_sweep_hook)(void) = NULL;
void sp_gc_promote_slot(sp_gc_hdr *head, sp_gc_hdr *tail, size_t bytes) {
  if (!head) return;
  tail->next = sp_gc_old_heap;
  sp_gc_old_heap = head;
  sp_gc_old_bytes += bytes;
  sp_gc_bytes += bytes;
}
#endif
void sp_gc_collect(void){
  size_t ob_before = sp_gc_bytes;
  int full=(sp_gc_cycle%SP_GC_FULL_INTERVAL==0);sp_gc_cycle++;
  /* new mark generation: every object becomes unmarked without touching it.
     On the (30-bit) wrap, clear the whole heap once so no stale stamp can
     alias the reused generation value. */
  sp_gc_mark_gen=(sp_gc_mark_gen+1)&0x1fffffffu;   /* marked is 29 bits (see sp_gc_hdr) */
  if(!sp_gc_mark_gen){
    sp_gc_mark_gen=1;
    for(sp_gc_hdr*hh=sp_gc_old_heap;hh;hh=hh->next)hh->marked=0;
#ifdef SP_THREADS
    { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS; for(int i=0;i<n;i++)for(sp_gc_hdr*hh=sp_gc_wslot[i].young;hh;hh=hh->next)hh->marked=0; }
#else
    for(sp_gc_hdr*hh=sp_gc_heap;hh;hh=hh->next)hh->marked=0;
#endif
  }
  /* Opt-in while the barrier's coverage is being completed: the emitted stores
     are covered, the runtime's container mutators are being swept through, and
     SPINEL_GC_VERIFY_GEN is what finds what is left. Default off means the
     collector behaves exactly as it did before the barrier landed. */
  sp_gc_minor = sp_gc_minor_on && !full && !sp_gc_rem_overflow;
  sp_gc_mark_all();
  if(sp_gc_minor){
    /* the remembered set is the rest of the root set for a minor: each entry is
       an old object holding a reference the walk above did not follow. */
    sp_gc_minor = 0;
    for(int ri=0;ri<sp_gc_nremembered;ri++){
      sp_gc_hdr *rh=(sp_gc_hdr*)sp_gc_remembered[ri]-1;
      if(rh->scan) rh->scan(sp_gc_remembered[ri]);
    }
    sp_gc_mark_drain();
  }
  sp_gc_minor = 0;
  /* Verification: re-run the mark whole-heap and compare. Anything the full
     mark reaches that the minor did not is a reference the barrier failed to
     record -- the one failure mode of this design, silent until it is a use
     after free. Off unless SPINEL_GC_VERIFY_GEN is set. */
  if(!full && sp_gc_verify_gen){
    /* Snapshot the young objects the minor did NOT reach, then mark whole-heap
       and see which of them the full mark does: each one is held only through
       an old object the barrier failed to record. Without the snapshot the
       full mark's own stamps make the two indistinguishable. */
    unsigned minor_gen = sp_gc_mark_gen;
    size_t cap = 4096, n = 0;
    sp_gc_hdr **cand = (sp_gc_hdr **)malloc(sizeof(sp_gc_hdr *) * cap);
    if (cand) {
#ifdef SP_THREADS
      { int w=sp_active_workers; if(w<1)w=1; if(w>SP_MAX_WORKERS)w=SP_MAX_WORKERS;
        for(int i=0;i<w;i++)
          for(sp_gc_hdr*h=sp_gc_wslot[i].young;h;h=h->next)
            if(!h->old && h->marked!=minor_gen){
              if(n==cap){cap*=2;cand=(sp_gc_hdr**)realloc(cand,sizeof(sp_gc_hdr*)*cap);if(!cand)break;}
              cand[n++]=h; } }
#else
      for(sp_gc_hdr*h=sp_gc_heap;h;h=h->next)
        if(!h->old && h->marked!=minor_gen){
          if(n==cap){cap*=2;cand=(sp_gc_hdr**)realloc(cand,sizeof(sp_gc_hdr*)*cap);if(!cand)break;}
          cand[n++]=h; }
#endif
    }
    sp_gc_mark_gen = (sp_gc_mark_gen + 1) & 0x1fffffffu;
    if(!sp_gc_mark_gen) sp_gc_mark_gen = 1;
    sp_gc_mark_all();
    size_t leaked = 0;
    for(size_t i=0;cand&&i<n;i++) if(cand[i]->marked==sp_gc_mark_gen) leaked++;
    if(leaked){
      fprintf(stderr,"spinel: GC generational check: %zu young object(s) reachable only "
                     "through an old one the barrier did not record\n", leaked);
      /* Name the holders: an old object that reaches one of them is where the
         missing barrier is, and its scan function names the type. */
      for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next){
        if(h->dirty||!h->scan) continue;
        sp_gc_verify_probe_hit=0; sp_gc_verify_probe=sp_gc_mark_gen; sp_gc_verify_probe_on=1;
        h->scan((char*)h+sizeof(sp_gc_hdr));
        sp_gc_verify_probe_on=0;
        if(sp_gc_verify_probe_hit) fprintf(stderr,"spinel:   holder scan=%p\n",(void*)h->scan);
      }
      sp_gc_verify_gen_fail = 1;
    }
    free(cand);
  }
  if(full){
    sp_gc_hdr**pp=&sp_gc_old_heap;sp_gc_old_bytes=0;
    while(*pp){sp_gc_hdr*h=*pp;if(h->marked!=sp_gc_mark_gen){*pp=h->next;if(h->recycle){h->recycle(h);}
    else{if(h->finalize)h->finalize((char*)h+sizeof(sp_gc_hdr));free(h);}}
    else{h->dirty=0;sp_gc_old_bytes+=h->size;pp=&h->next;}}
  }
  /* minor: the old list is not walked at all -- an old object's stale stamp
     simply reads as unmarked next generation, which is what a fresh unmark
     pass used to produce. */
  sp_gc_bytes=sp_gc_old_bytes;
#ifdef SP_THREADS
  { int n=sp_active_workers; if(n<1)n=1; if(n>SP_MAX_WORKERS)n=SP_MAX_WORKERS;
    /* Hand each parked worker its own slot. Only with a pool worth the barrier
       round trip: below that the serial walk the collector has always done is
       cheaper than waking everyone. */
    if (sp_gc_par_sweep_hook && n > 1) sp_gc_par_sweep_hook();
    else for(int i=0;i<n;i++)sp_gc_sweep_young(&sp_gc_wslot[i].young);
    /* The recompute above set sp_gc_bytes from every live object's size, so the
       workers' unflushed per-worker deltas are now subsumed -- clear them (all
       mutators are parked, so this is race-free). */
    for(int i=0;i<n;i++)sp_gc_wslot[i].flush_delta=0; }
#else
  sp_gc_sweep_young(&sp_gc_heap);
#endif
  /* The remembered set has done its job and starts over after EVERY cycle, not
     only a full one. Every young object it led the mark to has just been
     promoted by the sweep above, so a holder that is not written to again has
     nothing left to record; one that is gets recorded afresh by sp_gc_wb.
     Keeping entries until the next full cycle instead made the array grow
     monotonically and overflow on any real workload.

     The clear cannot go through the array alone. Once it has overflowed,
     objects carry dirty=1 with no entry in it, and clearing only what the
     array holds leaves them permanently dirty -- so sp_gc_wb's `!h->dirty`
     test rejects them forever and every young object they later point at is
     invisible to the minor mark. That is a silent use-after-free, and it is
     why the overflow path has to pay for the whole-heap walk. */
  if(full){
    /* the old sweep above cleared every survivor; the array may name objects it
       just freed, so it must not be walked here */
  }
#ifdef SP_THREADS
  /* A worker can be preempted between sp_gc_wb's `h->dirty = 1` and its push,
     so an object can carry the bit without an entry -- and clearing only what
     the array holds would leave it dirty forever, which is the one state that
     turns off its barrier for good. Single-threaded, no collection can start
     inside sp_gc_wb, so the array is exact and the cheap clear is correct. */
  else{ for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next)h->dirty=0; }
#else
  else if(sp_gc_rem_overflow){
    for(sp_gc_hdr*h=sp_gc_old_heap;h;h=h->next)h->dirty=0;
  }
  else{
    for(int ri=0;ri<sp_gc_nremembered;ri++)((sp_gc_hdr*)sp_gc_remembered[ri]-1)->dirty=0;
  }
#endif
  sp_gc_nremembered=0; sp_gc_rem_overflow=0;
  /* Sweep the string heap only when IT is over its trigger: the sweep is a
     full walk of the live string list, and running it on every OBJECT-heap
     collection made each collection O(live strings) -- the dominant cost of
     an allocation-heavy run. Skipping is safe: string marks accumulate, so a
     dead string at worst survives until the next string sweep (delayed
     reclamation, not a leak), and the sweep itself resets marks for the next
     cycle. The retune keeps the trigger tracking the live size. */
  /* Only on a full cycle now. The string heap has no generation of its own, so
     its sweep frees anything the mark did not reach -- and a minor mark does
     not reach a string held by an old object, because it does not walk old
     objects at all. Sweeping strings on a minor cycle therefore reaped live
     ones (test/file_basename_gc). Deferring to the full cycle is the same
     delayed reclamation the trigger already allowed. */
  if((full||!sp_gc_minor_on)&&sp_gc_str_sweep_hook)sp_gc_str_sweep_hook();
  /* malloc_trim walks the allocator arena; once per full cycle was ~10% of
     collection time on allocation-heavy runs. Every 4th full keeps the RSS
     benefit at a fraction of the cost. */
  if(full&&(sp_gc_cycle%(SP_GC_FULL_INTERVAL*4))==1)malloc_trim(0);
  if(sp_gc_obj_retune_hook)sp_gc_obj_retune_hook(ob_before);
}

/* Issue #1302: optional RSS ceiling via SPINEL_MAX_HEAP_MB; checked only
 * at GC-trigger points against real /proc/self/statm RSS. Default off. */
void sp_gc_enforce_mem_limit(void){
  if(!sp_gc_max_bytes_init){const char*e=getenv("SPINEL_MAX_HEAP_MB");long v=(e&&*e)?atol(e):0;sp_gc_max_bytes=(v>0)?(size_t)v*1024*1024:0;sp_gc_max_bytes_init=1;}
  if(!sp_gc_max_bytes)return;
#if defined(__linux__)
  FILE*sf=fopen("/proc/self/statm","r");if(!sf)return;long tot=0,res=0;int n=fscanf(sf,"%ld %ld",&tot,&res);fclose(sf);if(n!=2||res<=0)return;
  size_t rss=(size_t)res*(size_t)sysconf(_SC_PAGESIZE);
  if(rss>sp_gc_max_bytes){fprintf(stderr,"unhandled exception: out of memory (RSS %zu MB exceeded SPINEL_MAX_HEAP_MB=%zu MB)\n",rss/(1024*1024),sp_gc_max_bytes/(1024*1024));exit(1);}
#endif
}
