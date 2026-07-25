#include "analyze_internal.h"
#include <limits.h>

int is_builtin_class_name(const char *n) {
  if (!n) return 0;
  static const char *const CL[] = {
    "Integer","Float","String","Symbol","Array","Hash","Range","Time",
    "Module","Class","NilClass","TrueClass","FalseClass","Numeric",
    "Comparable","Enumerable","Object","BasicObject","Proc","Kernel",
    "IO","File","Exception","StandardError","RuntimeError","TypeError",
    "ArgumentError","NameError","NoMethodError","StopIteration","Math",
    "Complex","Rational","Encoding","Method","UnboundMethod","Fiber",
    "Thread","Mutex","GC","ObjectSpace","Signal","Process","Regexp",
    "MatchData","Enumerator","Struct","Data",
    /* exception classes as first-class values (raise_error matchers,
       e.class comparisons); raise/rescue position resolves by name. */
    "IndexError","KeyError","RangeError","FloatDomainError",
    "ZeroDivisionError","FrozenError","IOError","LocalJumpError",
    "NotImplementedError","ScriptError","SyntaxError","SecurityError",
    "RegexpError","EncodingError","SignalException","Interrupt",
    "ThreadError","FiberError","ClosedQueueError","UncaughtThrowError",
    "NoMatchingPatternError","NoMatchingPatternKeyError","EOFError",
    "SystemExit","Dir",NULL
  };
  /* the socket classes are constants only after `require "socket"` (CRuby
     defines them there too), so they are checked separately */
  if (sp_feature_required("socket")) {
    static const char *const SOCK[] = {
      "BasicSocket","IPSocket","TCPSocket","TCPServer",
      "UDPSocket","UNIXSocket","UNIXServer","Socket",NULL };
    for (int i = 0; SOCK[i]; i++) if (sp_streq(n, SOCK[i])) return 1;
  }
  for (int i = 0; CL[i]; i++) if (sp_streq(n, CL[i])) return 1;
  return 0;
}
/* Builtin constants that are MODULES in CRuby: `module Kernel` reopens them
   legally, while `module String` (a class) is CRuby's TypeError. */
int is_builtin_module_name(const char *n) {
  if (!n) return 0;
  static const char *const MODS[] = {
    "Comparable","Enumerable","Kernel","Math","GC","ObjectSpace",
    "Signal","Process", NULL
  };
  for (int i = 0; MODS[i]; i++) if (sp_streq(n, MODS[i])) return 1;
  return 0;
}
int is_builtin_exception_name(const char *n) {
  if (!n) return 0;
  static const char *const EXC[] = {
    "Exception", "StandardError", "RuntimeError", "ArgumentError",
    "TypeError", "NameError", "NoMethodError", "IndexError",
    "KeyError", "RangeError", "IOError", "EOFError", "Errno::ENOENT",
    "ZeroDivisionError", "NotImplementedError", "StopIteration",
    "FloatDomainError", "FrozenError", "EncodingError", "LoadError",
    "SystemExit", "Interrupt", "ScriptError", "SyntaxError",
    "RegexpError", "SecurityError", "SignalException", "ThreadError",
    "FiberError", "ClosedQueueError", "UncaughtThrowError",
    "NoMatchingPatternError", "NoMatchingPatternKeyError",
    "LocalJumpError", "Math::DomainError", "SystemCallError",
    "StringScanner_Error", "NoMemoryError", "SystemStackError",
    /* the non-blocking readiness family: two modules plus their Errno
       subclasses, so `rescue IO::WaitReadable` walks the hierarchy */
    "IO::WaitReadable", "IO::WaitWritable",
    "IO::EAGAINWaitReadable", "IO::EAGAINWaitWritable",
    "IO::EWOULDBLOCKWaitReadable", "IO::EWOULDBLOCKWaitWritable",
    "IO::EINPROGRESSWaitReadable", "IO::EINPROGRESSWaitWritable", NULL
  };
  for (int i = 0; EXC[i]; i++) if (sp_streq(n, EXC[i])) return 1;
  /* the Errno:: family is open -- the runtime picks a name from errno -- so
     any qualified Errno name is an exception class (#2922 follow-up) */
  if (strncmp(n, "Errno::", 7) == 0) return 1;
  return 0;
}
int class_inherits_builtin_exception(Compiler *c, int ci) {
  for (int k = ci; k >= 0; k = c->classes[k].parent) {
    int sc = nt_ref(c->nt, c->classes[k].def_node, "superclass");
    if (sc < 0) continue;
    const char *sty = nt_type(c->nt, sc);
    if (sty && sp_streq(sty, "ConstantReadNode") &&
        is_builtin_exception_name(nt_str(c->nt, sc, "name")))
      return 1;
    if (sty && sp_streq(sty, "ConstantPathNode") &&
        is_builtin_exception_name(nt_str(c->nt, sc, "name")))
      return 1;
  }
  return 0;
}
int an_re_has_captures(const char *src) {
  if (!src) return 0;
  int in_class = 0;
  for (const char *p = src; *p; p++) {
    if (*p == '\\') { if (p[1]) p++; continue; }
    /* a `(` inside a `[...]` character class is a literal paren, not a group
       (`/[()]/` has no captures) (#2912) */
    if (in_class) { if (*p == ']') in_class = 0; continue; }
    if (*p == '[') { in_class = 1; continue; }
    if (*p == '(') {
      if (p[1] != '?') return 1;
      /* (?<name>...) and (?'name'...) are named CAPTURE groups
         ((?<=..)/(?<!..) lookbehinds are not) */
      if (p[1] == '?' && p[2] == '<' && p[3] != '=' && p[3] != '!') return 1;
      if (p[1] == '?' && p[2] == 0x27) return 1;
    }
  }
  return 0;
}
int str_in(const char *s, const char *const *set) {
  if (!s) return 0;
  for (int i = 0; set[i]; i++) if (sp_streq(s, set[i])) return 1;
  return 0;
}
int is_arith_op(const char *op) {
  static const char *const set[] = {"+", "-", "*", "/", "%", "**", NULL};
  return str_in(op, set);
}
int is_cmp_op(const char *op) {
  static const char *const set[] = {"<", ">", "<=", ">=", NULL};
  return str_in(op, set);
}
int is_eq_op(const char *op) {
  static const char *const set[] = {"==", "!=", "===", NULL};
  return str_in(op, set);
}
int is_void_call(const char *name) {
  static const char *const set[] = {
    "puts", "print", "p", "pp", "require", "require_relative",
    "raise", "warn", "printf", NULL};
  return str_in(name, set);
}
/* A local variable that statically holds exactly one user class (every write
   in its scope assigns the same class constant) resolves to that class index;
   -1 when dynamic. Lets `k = Klass; k.new(...)` and `k.members` dispatch
   statically. */
int class_var_static_ci(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, node);
  /* an inline `Struct.new(...)` / `Data.define(...)` receiver resolves to the
     anon class synthesized for that call node (#2682) */
  if (ty && sp_streq(ty, "CallNode") && is_struct_call(c, node))
    return anon_struct_ci_for_value(c, node);
  if (!ty || !sp_streq(ty, "LocalVariableReadNode")) return -1;
  const char *vn = nt_str(nt, node, "name");
  if (!vn) return -1;
  Scope *sc = comp_scope_of(c, node);
  int found = -1;
  for (int w = comp_lvw_first(c, vn); w >= 0; w = comp_lvw_next(c, w)) {
    const char *wn = nt_str(nt, w, "name");
    if (!wn || !sp_streq(wn, vn) || comp_scope_of(c, w) != sc) continue;
    int val = nt_ref(nt, w, "value");
    const char *vty = val >= 0 ? nt_type(nt, val) : NULL;
    int ci = (vty && sp_streq(vty, "ConstantReadNode"))
             ? comp_class_index(c, nt_str(nt, val, "name")) : -1;
    /* k = Struct.new(:a, :b): resolve to the anonymous struct class that
       register_structs synthesized for this write (keyed by def_node) */
    if (ci < 0 && is_struct_call(c, val)) {
      for (int k = 0; k < c->nclasses; k++)
        if (c->classes[k].is_anon_struct && c->classes[k].def_node == w) { ci = k; break; }
    }
    if (ci < 0) return -1;                   /* a non-class write: dynamic */
    if (found >= 0 && found != ci) return -1; /* two classes: dynamic */
    found = ci;
  }
  return found;
}

/* A local variable that statically holds exactly one BUILTIN class constant
   (every write in its scope assigns the same builtin class name): that name,
   or NULL. The user-class analogue is class_var_static_ci; builtins have no
   class index, so this resolves by name (#2715). */
const char *builtin_class_var_static_name(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  if (node < 0 || nt_kind(nt, node) != NK_LocalVariableReadNode) return NULL;
  const char *vn = nt_str(nt, node, "name");
  if (!vn) return NULL;
  Scope *sc = comp_scope_of(c, node);
  const char *found = NULL;
  for (int w = comp_lvw_first(c, vn); w >= 0; w = comp_lvw_next(c, w)) {
    const char *wn = nt_str(nt, w, "name");
    if (!wn || !sp_streq(wn, vn) || comp_scope_of(c, w) != sc) continue;
    int val = nt_ref(nt, w, "value");
    const char *cn = (val >= 0 && nt_kind(nt, val) == NK_ConstantReadNode)
                     ? nt_str(nt, val, "name") : NULL;
    /* a USER class constant qualifies too: the retargeted receiver then rides
       every ConstantReadNode dispatch arm (method_defined?, subclasses,
       class_eval, ...), not just the sites class_var_static_ci was wired into
       (#2717, #2721) */
    if (!cn || !(is_builtin_class_name(cn) || comp_class_index(c, cn) >= 0)) return NULL;
    if (found && !sp_streq(found, cn)) return NULL;   /* two classes: dynamic */
    found = cn;
  }
  return found;
}

/* The literal symbol behind a symbol-typed expression: a SymbolNode itself,
   or a local variable whose only write (in its scope, plain write) is one.
   Lets inject(:op)-style operator selection see through `s = :+; a.inject(s)`.
   Returns the symbol's name, or NULL. */
const char *sym_static_value(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  if (node < 0) return NULL;
  const char *ty = nt_type(nt, node);
  if (ty && sp_streq(ty, "SymbolNode")) return nt_str(nt, node, "value");
  if (!ty || !sp_streq(ty, "LocalVariableReadNode")) return NULL;
  const char *vn = nt_str(nt, node, "name");
  if (!vn) return NULL;
  Scope *sc = comp_scope_of(c, node);
  int val = -1;
  for (int w = 0; w < nt->count; w++) {
    NodeKind k = nt_kind(nt, w);
    if (k != NK_LocalVariableWriteNode && k != NK_LocalVariableOrWriteNode &&
        k != NK_LocalVariableAndWriteNode && k != NK_LocalVariableOperatorWriteNode &&
        k != NK_LocalVariableTargetNode)
      continue;
    const char *wn = nt_str(nt, w, "name");
    if (!wn || !sp_streq(wn, vn)) continue;
    if (k != NK_LocalVariableWriteNode || comp_scope_of(c, w) != sc || val >= 0)
      return NULL;
    val = nt_ref(nt, w, "value");
  }
  if (val < 0) return NULL;
  const char *vty = nt_type(nt, val);
  return (vty && sp_streq(vty, "SymbolNode")) ? nt_str(nt, val, "value") : NULL;
}

/* A CallNode chain that evaluates to a Lazy (a `.lazy` somewhere in the
   receiver spine, and a lazy-producing transform -- not a forcing terminal --
   on top). Such a value has no runtime representation in spinel; it is only
   ever fused at a forcing site. (#2932) */
/* See compiler.h for the masks. Every name carries SP_MUT_LOCAL; the
   narrower sites drop the mutators their storage shape cannot serve:
     `[]=`                      rebinds through an index write, no container arm
     insert / slice! / setbyte  need the rename+shadow shim, impossible on an ivar
     append_as_bytes            has no guard-narrowed re-route arm
   The masks reproduce the four lists this table replaced, name for name. */
int sp_str_mutator(const char *nm, unsigned want) {
  static const struct { const char *nm; unsigned mask; } M[] = {
    { "[]=",             SP_MUT_LOCAL },
    { "insert",          SP_MUT_LOCAL | SP_MUT_CONTAINER },
    { "slice!",          SP_MUT_LOCAL | SP_MUT_CONTAINER },
    { "setbyte",         SP_MUT_LOCAL | SP_MUT_CONTAINER },
    { "append_as_bytes", SP_MUT_LOCAL | SP_MUT_CONTAINER | SP_MUT_IVAR },
    { "<<",              15u }, { "concat",         15u }, { "prepend",    15u },
    { "replace",         15u }, { "clear",          15u }, { "bytesplice", 15u },
    { "gsub!",           15u }, { "sub!",           15u }, { "upcase!",    15u },
    { "downcase!",       15u }, { "capitalize!",    15u }, { "swapcase!",  15u },
    { "strip!",          15u }, { "lstrip!",        15u }, { "rstrip!",    15u },
    { "chomp!",          15u }, { "chop!",          15u }, { "squeeze!",   15u },
    { "tr!",             15u }, { "delete!",        15u }, { "tr_s!",      15u },
    { "delete_prefix!",  15u }, { "delete_suffix!", 15u }, { "reverse!",   15u },
    { "succ!",           15u }, { "next!",          15u },
    { NULL, 0 }
  };
  if (!nm) return 0;
  for (int i = 0; M[i].nm; i++)
    if (sp_streq(nm, M[i].nm)) return (M[i].mask & want) == want;
  return 0;
}

int lazy_stage_name(const char *nm) {
  static const char *const ST[] = {
    /* re-lazy on an already-lazy chain is transparent */
    "lazy",
    /* block stages (LAZY_BLOCK_STAGE in codegen_call.c) */
    "map", "collect", "select", "filter", "find_all", "reject",
    "take_while", "drop_while", "filter_map", "flat_map", "collect_concat",
    /* blockless counter / grouping stages (LAZY_COUNTER_STAGE) */
    "take", "drop", "each_slice", "each_cons", NULL };
  if (!nm) return 0;
  for (int i = 0; ST[i]; i++) if (sp_streq(nm, ST[i])) return 1;
  return 0;
}

int chain_is_lazy_valued(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  if (node < 0 || !nt_type(nt, node) || !sp_streq(nt_type(nt, node), "CallNode")) return 0;
  const char *top = nt_str(nt, node, "name");
  if (!top) return 0;
  /* with_index / zip keep the VALUE lazy (so a chain ending in them is still
     a Lazy for recognition purposes) but the pipeline cannot fuse them, so
     they are not stages -- the write-suppression walk must not accept them. */
  if (!lazy_stage_name(top) && !sp_streq(top, "with_index") && !sp_streq(top, "zip"))
    return 0;
  int hops = 0;
  for (int cur = node; cur >= 0; ) {
    /* part of the chain may be held in a variable
       (`s = src.lazy.select{}; s.each_cons(2).lazy...`): resolve the
       single-plain-write alias and keep walking. Bounded: a
       self-referential write (`s = s.map{}`) would otherwise recurse
       through lazy_alias_chain forever (#3324). */
    if (nt_type(nt, cur) && sp_streq(nt_type(nt, cur), "LocalVariableReadNode")) {
      if (++hops > 8) return 0;
      int a = lazy_alias_chain(c, cur);
      if (a < 0) return 0;
      cur = a;
      continue;
    }
    if (!nt_type(nt, cur) || !sp_streq(nt_type(nt, cur), "CallNode")) return 0;
    const char *nm = nt_str(nt, cur, "name");
    if (nm && sp_streq(nm, "lazy") && nt_ref(nt, cur, "block") < 0) return 1;
    cur = nt_ref(nt, cur, "receiver");
  }
  return 0;
}

/* If VAR (a LocalVariableReadNode) is a single-plain-write alias for a lazy
   chain in the same scope, return the chain node so a forcing call on the
   variable can fuse it as if written inline; else -1. Models sym_static_value.
   (#2932) */
int lazy_alias_chain(Compiler *c, int var_read) {
  const NodeTable *nt = c->nt;
  if (var_read < 0 || nt_kind(nt, var_read) != NK_LocalVariableReadNode) return -1;
  const char *vn = nt_str(nt, var_read, "name");
  if (!vn) return -1;
  Scope *sc = comp_scope_of(c, var_read);
  int val = -1;
  for (int w = 0; w < nt->count; w++) {
    NodeKind k = nt_kind(nt, w);
    if (k != NK_LocalVariableWriteNode && k != NK_LocalVariableOrWriteNode &&
        k != NK_LocalVariableAndWriteNode && k != NK_LocalVariableOperatorWriteNode &&
        k != NK_LocalVariableTargetNode)
      continue;
    const char *wn = nt_str(nt, w, "name");
    if (!wn || !sp_streq(wn, vn)) continue;
    if (k != NK_LocalVariableWriteNode || comp_scope_of(c, w) != sc || val >= 0)
      return -1;   /* not a single plain write */
    val = nt_ref(nt, w, "value");
  }
  if (val < 0) return -1;
  return chain_is_lazy_valued(c, val) ? val : -1;
}

/* A bare call to a parameterless user method whose whole body is a lazy chain
   resolves to that chain, so `def lz; [1,2,3].lazy; end; lz.first` fuses the
   same way the inline form does. A lazy value has no runtime representation to
   return, so without this the method body is simply not emittable (#3358).
   Restricted to a self-independent chain -- no self, ivar, or local read --
   because the chain is re-emitted at the CALL SITE, where the callee's
   receiver and locals are not in scope. */
static int lazy_chain_self_free(Compiler *c, int node, int depth) {
  const NodeTable *nt = c->nt;
  if (node < 0 || depth > 32) return 0;
  NodeKind k = nt_kind(nt, node);
  if (k == NK_SelfNode || k == NK_InstanceVariableReadNode ||
      k == NK_LocalVariableReadNode || k == NK_ClassVariableReadNode)
    return 0;
  int nr = nt_num_refs(nt, node);
  for (int i = 0; i < nr; i++) {
    int ch = nt_ref_at(nt, node, i);
    /* a block body binds its own params, so its reads are its own */
    if (ch >= 0 && nt_kind(nt, ch) == NK_BlockNode) continue;
    if (ch >= 0 && !lazy_chain_self_free(c, ch, depth + 1)) return 0;
  }
  int na = nt_num_arrs(nt, node);
  for (int i = 0; i < na; i++) {
    int n = 0; const int *ids = nt_arr_at(nt, node, i, &n);
    for (int j = 0; j < n; j++)
      if (ids[j] >= 0 && nt_kind(nt, ids[j]) != NK_BlockNode &&
          !lazy_chain_self_free(c, ids[j], depth + 1)) return 0;
  }
  return 1;
}
int lazy_method_chain(Compiler *c, int call) {
  const NodeTable *nt = c->nt;
  if (call < 0 || nt_kind(nt, call) != NK_CallNode) return -1;
  if (nt_ref(nt, call, "receiver") >= 0 || nt_ref(nt, call, "block") >= 0) return -1;
  { int ar = nt_ref(nt, call, "arguments"); int ac = 0;
    if (ar >= 0) nt_arr(nt, ar, "arguments", &ac);
    if (ac != 0) return -1; }
  const char *nm = nt_str(nt, call, "name");
  if (!nm) return -1;
  int m = comp_method_index(c, nm);
  if (m < 0 || c->scopes[m].nparams != 0) return -1;
  int body = c->scopes[m].body;
  if (body < 0) return -1;
  int n = 0; const int *st = nt_arr(nt, body, "body", &n);
  if (n != 1 || !st) return -1;
  if (!chain_is_lazy_valued(c, st[0])) return -1;
  return lazy_chain_self_free(c, st[0], 0) ? st[0] : -1;
}

/* The anonymous struct class synthesized for a `k = Struct.new(:a, :b)`
   VALUE node (the write is the class's def_node), or -1. Lets the value
   type as TY_CLASS and emit as the class object. */
/* The first member name that appears more than once in a Struct.new(...) /
   Data.define(...) call's argument list, or NULL. CRuby raises ArgumentError
   ("duplicate member") for such a definition (#2705). */
const char *struct_call_dup_member(Compiler *c, int callnode) {
  const NodeTable *nt = c->nt;
  if (callnode < 0) return NULL;
  int args = nt_ref(nt, callnode, "arguments");
  int an = 0; const int *argv = args >= 0 ? nt_arr(nt, args, "arguments", &an) : NULL;
  if (!argv) return NULL;
  for (int a = 0; a < an; a++) {
    if (!nt_type(nt, argv[a]) || !sp_streq(nt_type(nt, argv[a]), "SymbolNode")) continue;
    const char *m = nt_str(nt, argv[a], "value");
    if (!m) continue;
    for (int b = 0; b < a; b++) {
      if (!nt_type(nt, argv[b]) || !sp_streq(nt_type(nt, argv[b]), "SymbolNode")) continue;
      const char *m2 = nt_str(nt, argv[b], "value");
      if (m2 && sp_streq(m2, m)) return m;
    }
  }
  return NULL;
}

int anon_struct_ci_for_value(Compiler *c, int val) {
  const NodeTable *nt = c->nt;
  if (val < 0) return -1;
  /* Anonymous structs number a handful at most while c->classes can hold
     hundreds, and this runs per candidate receiver: collect the anon subset
     once instead of rescanning every class on each call. */
  if (!c->anon_struct_ids_valid) {
    int *ids = malloc((size_t)(c->nclasses > 0 ? c->nclasses : 1) * sizeof(int));
    if (ids) {
      free(c->anon_struct_ids);
      c->anon_struct_ids = ids;
      c->n_anon_struct_ids = 0;
      for (int k = 0; k < c->nclasses; k++)
        if (c->classes[k].is_anon_struct) c->anon_struct_ids[c->n_anon_struct_ids++] = k;
      c->anon_struct_ids_valid = 1;
    } else {
      for (int k = 0; k < c->nclasses; k++) {
        if (!c->classes[k].is_anon_struct) continue;
        int w = c->classes[k].def_node;
        if (w >= 0 && (nt_ref(nt, w, "value") == val || w == val)) return k;
      }
      return -1;
    }
  }
  for (int q = 0; q < c->n_anon_struct_ids; q++) {
    int k = c->anon_struct_ids[q];
    int w = c->classes[k].def_node;
    /* keyed by a write node (k = Struct.new -> def_node's value is the call),
       or, for an inline `Data.define(...).method(...)` receiver, keyed by the
       struct/data call node itself (def_node IS that call). #2682 */
    if (w >= 0 && (nt_ref(nt, w, "value") == val || w == val)) return k;
  }
  return -1;
}

/* `Hash.new(d)` (or the desugared __hash_new_default) as a bare expression:
   return the default-value argument node, or -1. Lets a literal receiver's
   .default / [] / fetch fold to the default when the hash type never
   narrows (no writes ever reach it). */
static int hash_new_default_arg_compute(Compiler *c, int recv);

/* hash_new_default_arg's result is a pure function of the (static) AST node
   table plus scope assignments, which are fixed while the scope-index is
   frozen. The local-variable branch scans every write node in the program, and
   the function is called for every `x[k]` / `x.default` on an untyped receiver
   during the inference fixpoint -- O(N) per call, O(N^2) overall. Memoize per
   receiver node while frozen, discarding the cache when the scope epoch changes
   (see comp_scope_index_gen); fall back to a direct compute while unfrozen. */
int hash_new_default_arg(Compiler *c, int recv) {
  if (recv < 0) return -1;
  if (!comp_scope_index_is_frozen() || recv >= c->node_cap)
    return hash_new_default_arg_compute(c, recv);
  unsigned gen = comp_scope_index_gen();
  if (!c->hash_default_arg_memo || c->hash_default_arg_memo_cap < c->node_cap) {
    free(c->hash_default_arg_memo);
    c->hash_default_arg_memo = malloc((size_t)c->node_cap * sizeof(int));
    c->hash_default_arg_memo_cap = c->hash_default_arg_memo ? c->node_cap : 0;
    c->hash_default_arg_memo_gen = gen - 1u; /* force the re-init below */
  }
  if (!c->hash_default_arg_memo) return hash_new_default_arg_compute(c, recv);
  if (c->hash_default_arg_memo_gen != gen) {
    for (int i = 0; i < c->hash_default_arg_memo_cap; i++) c->hash_default_arg_memo[i] = INT_MIN;
    c->hash_default_arg_memo_gen = gen;
  }
  if (c->hash_default_arg_memo[recv] != INT_MIN) return c->hash_default_arg_memo[recv];
  /* Mark the slot resolved-but-dynamic (-1) before recomputing: the compute
     recurses through a local's writes, which can re-enter for this same node
     via a write cycle (`a = b; b = a`). -1 is the correct conservative result
     for such a cyclic (non-single-Hash.new) definition and breaks the cycle;
     the real result overwrites it below. */
  c->hash_default_arg_memo[recv] = -1;
  int r = hash_new_default_arg_compute(c, recv);
  c->hash_default_arg_memo[recv] = r;
  return r;
}

static int hash_new_default_arg_compute(Compiler *c, int recv) {
  const NodeTable *nt = c->nt;
  if (recv < 0 || !nt_type(nt, recv)) return -1;
  /* a local whose every same-scope write is the same Hash.new(d) shape
     resolves like the literal (`a = Hash.new(7); a.default`) */
  if (sp_streq(nt_type(nt, recv), "LocalVariableReadNode")) {
    const char *vn = nt_str(nt, recv, "name");
    if (!vn) return -1;
    Scope *sc = comp_scope_of(c, recv);
    if (!sc) return -1;
    int found = -1;
    for (int r = lw_shared_first(c, vn, (int)(sc - c->scopes)); r >= 0;
         r = lw_shared_next(r)) {
      int w = lw_shared_node(r);
      if (nt_kind(nt, w) != NK_LocalVariableWriteNode) continue;
      const char *wn = nt_str(nt, w, "name");
      if (!wn || !sp_streq(wn, vn) || comp_scope_of(c, w) != sc) continue;
      int val = nt_ref(nt, w, "value");
      int dn = val >= 0 ? hash_new_default_arg(c, val) : -1;
      if (dn < 0) return -1;                 /* a non-matching write: dynamic */
      if (found >= 0) return -1;             /* multiple writes: dynamic */
      found = dn;
    }
    return found;
  }
  if (!sp_streq(nt_type(nt, recv), "CallNode")) return -1;
  const char *nm = nt_str(nt, recv, "name");
  if (!nm || (!sp_streq(nm, "new") && !sp_streq(nm, "__hash_new_default"))) return -1;
  int cr = nt_ref(nt, recv, "receiver");
  if (cr < 0 || !nt_type(nt, cr) || !sp_streq(nt_type(nt, cr), "ConstantReadNode")) return -1;
  const char *cn = nt_str(nt, cr, "name");
  if (!cn || !sp_streq(cn, "Hash")) return -1;
  if (nt_ref(nt, recv, "block") >= 0) return -1;   /* block default: dproc path */
  int a = nt_ref(nt, recv, "arguments");
  int ac = 0;
  const int *av = a >= 0 ? nt_arr(nt, a, "arguments", &ac) : NULL;
  if (ac != 1 || !av) return -1;
  const char *aty = nt_type(nt, av[0]);
  if (aty && sp_streq(aty, "KeywordHashNode")) return -1;
  return av[0];
}

int struct_member_idx(Compiler *c, ClassInfo *sc, int keynode) {
  const NodeTable *nt = c->nt;
  const char *kty = nt_type(nt, keynode);
  if (!kty) return -1;
  if (sp_streq(kty, "SymbolNode") || sp_streq(kty, "StringNode")) {
    const char *kn = sp_streq(kty, "SymbolNode") ? nt_str(nt, keynode, "value")
                                                 : nt_str(nt, keynode, "content");
    if (!kn) return -1;
    char ivn[256]; snprintf(ivn, sizeof ivn, "@%s", kn);
    int iv = comp_ivar_index(sc, ivn);
    return iv;  /* ivar order == member order */
  }
  if (sp_streq(kty, "IntegerNode")) {
    int idx = (int)nt_int(nt, keynode, "value", INT_MIN);
    if (idx < 0) idx += sc->nivars;   /* negative counts from the last member */
    if (idx >= 0 && idx < sc->nivars) return idx;
  }
  return -1;
}
int scope_body_last(Compiler *c, int mi) {
  int body = c->scopes[mi].body;
  if (body < 0 || !nt_type(c->nt, body) || !sp_streq(nt_type(c->nt, body), "StatementsNode")) return -1;
  int n = 0; const int *bb = nt_arr(c->nt, body, "body", &n);
  return n > 0 ? bb[n - 1] : -1;
}
int is_blk_param_call(Compiler *c, int node, int mi) {
  const NodeTable *nt = c->nt;
  if (node < 0 || !nt_type(nt, node) || !sp_streq(nt_type(nt, node), "CallNode")) return 0;
  const char *nm = nt_str(nt, node, "name");
  if (!nm || (!sp_streq(nm, "call") && !sp_streq(nm, "()") && !sp_streq(nm, "[]"))) return 0;
  int recv = nt_ref(nt, node, "receiver");
  if (recv < 0 || !nt_type(nt, recv) || !sp_streq(nt_type(nt, recv), "LocalVariableReadNode")) return 0;
  const char *rn = nt_str(nt, recv, "name");
  const char *bp = c->scopes[mi].blk_param;
  return rn && bp && bp[0] && sp_streq(rn, bp);
}
int g_yvt_mi[MAX_YVT_DEPTH];
int g_yvt_depth = 0;
/* set by yield_value_diverges to make yield_value_type unify all call sites
   (rather than take the first concrete one) for divergence detection */
int g_yvt_unify_all = 0;
int an_ie_class_id = -1;
int g_cbody_class_id = -1;
int g_cbody_direct = -1;
TyKind scan_break_type(Compiler *c, int id, int depth) {
  if (id < 0 || depth > 32) return TY_UNKNOWN;
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, id);
  if (!ty) return TY_UNKNOWN;
  if (sp_streq(ty, "DefNode")) return TY_UNKNOWN;
  if (sp_streq(ty, "BlockNode") && depth > 0) return TY_UNKNOWN; /* inner block's breaks don't escape */
  if (sp_streq(ty, "BreakNode")) {
    int v = nt_ref(nt, id, "arguments");
    if (v < 0) return TY_NIL;
    int vargc = 0; const int *vargs = nt_arr(nt, v, "arguments", &vargc);
    if (vargc > 0) return infer_type(c, vargs[0]);
    return TY_NIL;
  }
  TyKind result = TY_UNKNOWN;
  int nr = nt_num_refs(nt, id);
  for (int i = 0; i < nr; i++) {
    TyKind t = scan_break_type(c, nt_ref_at(nt, id, i), depth + 1);
    if (t != TY_UNKNOWN) result = ty_unify(result, t);
  }
  int na = nt_num_arrs(nt, id);
  for (int i = 0; i < na; i++) {
    int n = 0; const int *ids = nt_arr_at(nt, id, i, &n);
    for (int k = 0; k < n; k++) {
      TyKind t = scan_break_type(c, ids[k], depth + 1);
      if (t != TY_UNKNOWN) result = ty_unify(result, t);
    }
  }
  return result;
}
TyKind scan_throw_type(Compiler *c, const char *tag) {
  /* Program-wide: a throw can reach its catch through any number of method
     frames, so the catch's value type must unify with every throw that can
     target it. A literal tag on both sides that differs is provably
     unreachable; a non-literal tag on either side always participates. */
  const NodeTable *nt = c->nt;
  TyKind result = TY_UNKNOWN;
  NT_FOREACH_KIND(nt, NK_CallNode, id) {
    const char *nm = nt_str(nt, id, "name");
    if (!nm || !sp_streq(nm, "throw") || nt_ref(nt, id, "receiver") >= 0) continue;
    int v = nt_ref(nt, id, "arguments");
    int vargc = 0; const int *vargs = v >= 0 ? nt_arr(nt, v, "arguments", &vargc) : NULL;
    if (vargc < 1) continue;
    const char *tty = nt_type(nt, vargs[0]);
    const char *tnm = NULL;
    if (tty && sp_streq(tty, "SymbolNode")) tnm = nt_str(nt, vargs[0], "value");
    else if (tty && sp_streq(tty, "StringNode")) tnm = nt_str(nt, vargs[0], "unescaped");
    if (tag && tnm && !sp_streq(tnm, tag)) continue;
    TyKind t = vargc >= 2 ? infer_type(c, vargs[1]) : TY_NIL;
    if (t != TY_UNKNOWN) result = ty_unify(result, t);
  }
  return result;
}
/* Call sites that pass a block (an explicit block node, or a `...` forward that
   carries one implicitly), cached per node table. yield_value_type scans for
   these once per method during the fixpoint; without the index that is
   O(methods * nodes * iterations). The structural shape is stable across the
   pass; the callee resolution below still runs fresh. */
static const NodeTable *yvt_nt = NULL;
static int yvt_ntc = -1, yvt_n = 0;
static int *yvt_ids = NULL;
static int yvt_call_forwards_block(const NodeTable *nt, int cid) {
  int a = nt_ref(nt, cid, "arguments");
  int an = 0; const int *av = a >= 0 ? nt_arr(nt, a, "arguments", &an) : NULL;
  return an == 1 && av && nt_type(nt, av[0]) &&
         sp_streq(nt_type(nt, av[0]), "ForwardingArgumentsNode");
}
static void yvt_build(Compiler *c) {
  const NodeTable *nt = c->nt;
  int n = nt->count;
  free(yvt_ids);
  yvt_ids = malloc((size_t)(n > 0 ? n : 1) * sizeof(int));
  yvt_n = 0;
  yvt_nt = nt; yvt_ntc = n;
  if (!yvt_ids) return;
  for (int cid = 0; cid < n; cid++) {
    const char *cty = nt_type(nt, cid);
    if (!cty || !sp_streq(cty, "CallNode")) continue;
    if (nt_ref(nt, cid, "block") < 0 && !yvt_call_forwards_block(nt, cid)) continue;
    yvt_ids[yvt_n++] = cid;
  }
}
TyKind yield_value_type(Compiler *c, int mi) {
  for (int i = 0; i < g_yvt_depth; i++)
    if (g_yvt_mi[i] == mi) return TY_UNKNOWN;
  if (g_yvt_depth >= MAX_YVT_DEPTH) return TY_UNKNOWN;
  g_yvt_mi[g_yvt_depth++] = mi;

  const NodeTable *nt = c->nt;
  TyKind result = TY_UNKNOWN;
  if (yvt_nt != nt || yvt_ntc != nt->count) yvt_build(c);
  for (int ii = 0; ii < yvt_n; ii++) {
    int cid = yvt_ids[ii];
    int blk = nt_ref(nt, cid, "block");
    /* A `callee(...)` forward carries its block implicitly inside the `...`
       (no explicit block node); treat it as a forwarded block too. */
    int fwd_args = yvt_call_forwards_block(nt, cid);
    /* skip calls that live inside method mi itself (recursive self-calls);
       only external call sites provide a concrete block value type */
    if ((int)(comp_scope_of(c, cid) - c->scopes) == mi) continue;
    const char *cn = nt_str(nt, cid, "name");
    int crecv = nt_ref(nt, cid, "receiver");
    int rmi = -1;
    if (crecv < 0) {
      rmi = comp_method_index(c, cn);
      if (rmi < 0) {
        Scope *cs = comp_scope_of(c, cid);
        if (cs->class_id >= 0) {
          rmi = comp_method_in_chain(c, cs->class_id, cn, NULL);
          /* an implicit-self call inside a class method resolves to a CLASS
             method; its block/forward feeds that method's blk_ret too */
          if (rmi < 0) rmi = comp_cmethod_in_chain(c, cs->class_id, cn, NULL);
        }
      }
    }
else {
      TyKind crt = infer_type(c, crecv);
      if (ty_is_object(crt)) rmi = comp_method_in_chain(c, ty_object_class(crt), cn, NULL);
      /* `Klass.new { block }`: the block feeds the class's initialize. A
         scoped receiver (`NS::Klass`) resolves by its leaf name, the key
         classes are indexed under. */
      else if (cn && sp_streq(cn, "new") && nt_type(nt, crecv) &&
               (sp_streq(nt_type(nt, crecv), "ConstantReadNode") ||
                sp_streq(nt_type(nt, crecv), "ConstantPathNode"))) {
        int nci = comp_class_index(c, nt_str(nt, crecv, "name"));
        if (nci >= 0) rmi = comp_method_in_chain(c, nci, "initialize", NULL);
      }
      /* `Klass.m { block }` / `Mod.m { block }` / `NS::Klass.m { block }`: a
         class/module self-method (singleton). Resolve the constant and look
         up its singleton method so the block's value type flows back to m's
         return type -- instance methods resolve via the ty_is_object arm
         above (#1446). */
      else if (nt_type(nt, crecv) &&
               (sp_streq(nt_type(nt, crecv), "ConstantReadNode") ||
                sp_streq(nt_type(nt, crecv), "ConstantPathNode"))) {
        int nci = comp_class_index(c, nt_str(nt, crecv, "name"));
        if (nci >= 0) rmi = comp_cmethod_in_chain(c, nci, cn, NULL);
      }
    }
    if (rmi != mi) continue;
    /* `mi(&b)` / `mi(...)`: the call forwards the block of its enclosing
       method rather than passing a literal. The value `mi` yields is then
       whatever that forwarded block produces -- the enclosing method's
       own yield value. */
    const char *blkty = blk >= 0 ? nt_type(nt, blk) : NULL;
    if (fwd_args || (blkty && sp_streq(blkty, "BlockArgumentNode"))) {
      Scope *encl = comp_scope_of(c, cid);
      int emi = encl ? (int)(encl - c->scopes) : -1;
      TyKind ft = (emi >= 0 && emi != mi) ? yield_value_type(c, emi) : TY_UNKNOWN;
      if (ft == TY_VOID) ft = TY_NIL;
      if (c->scopes[mi].yields || c->scopes[mi].is_lowered_yield) {
        if (ft != TY_UNKNOWN) { result = ft; break; }
        continue;
      }
      result = ty_unify(result, ft);
      continue;
    }
    int bb = nt_ref(nt, blk, "body");
    int bn = 0; const int *bd = bb >= 0 ? nt_arr(nt, bb, "body", &bn) : NULL;
    TyKind bt;
    if (bn == 0) bt = TY_NIL;
    else if (nt_type(nt, bd[bn - 1]) && sp_streq(nt_type(nt, bd[bn - 1]), "ReturnNode"))
      /* `{ return e }`: a non-local return — the yield never produces a
         value, but typing it as e's type keeps the enclosing method's
         return shape consistent (the inline emits `return e` directly). */
      bt = return_node_type(c, bd[bn - 1]);
    else bt = infer_type(c, bd[bn - 1]);
    if (bt == TY_VOID) bt = TY_NIL;
    /* A yield-inlined (or self-recursive-lowered) method is specialized per
       call site, so its internal block type is the first concrete block. A
       non-inlined method (an escaping &block called via the proc ABI) has one
       body, so its block value type must unify ALL call sites (string + int
       block -> poly). */
    if ((c->scopes[mi].yields || c->scopes[mi].is_lowered_yield) && !g_yvt_unify_all) { result = bt; break; }
    result = ty_unify(result, bt);
  }
  g_yvt_depth--;
  return result;
}
/* Whether a yield-inlined method's block value type DIVERGES across its call
   sites (one caller's block returns int, another's an object): the shared
   accumulator that receives yield results must then hold a poly element (an
   sp_IntArray built from an object-returning block miscompiles, #2454). Reuses
   yield_value_type's site scan with the first-concrete break disabled. */
int yield_value_diverges(Compiler *c, int mi) {
  if (mi < 0 || !c->scopes[mi].yields) return 0;
  int sv = g_yvt_unify_all; g_yvt_unify_all = 1;
  TyKind t = yield_value_type(c, mi);
  g_yvt_unify_all = sv;
  return t == TY_POLY;
}
/* The value type `node` contributes to an accumulator, widened to poly when it
   is a `yield` whose enclosing method's block value type diverges across call
   sites. One shared inlined body cannot size the accumulator for both an int-
   and a String-returning block: whichever site is analyzed first settles the
   slot, and the other emits into it. Used for an explicit `acc << yield(x)`
   (#2454) and for the collector an iterator builds from a `yield` block body
   -- `[x].map { |v| yield v }` inside a wrapper method (#2457). */
/* Does class ci descend from an explicit `< BasicObject` (a blank slate)?
   Such an instance answers only BasicObject's own methods and what the user
   defined; the Object/Kernel default arms must NOT serve it (#2703). */
int class_is_blank_slate(Compiler *c, int ci) {
  for (int k = ci; k >= 0; k = c->classes[k].parent) {
    int sc = nt_ref(c->nt, c->classes[k].def_node, "superclass");
    const char *sn = sc >= 0 ? nt_str(c->nt, sc, "name") : NULL;
    if (sn && sp_streq(sn, "BasicObject")) return 1;
    if (sn && c->classes[k].parent < 0) return 0;   /* rooted at another builtin */
  }
  return 0;
}

/* Does the program define a method of this name itself? A builtin fallback for
   a poly receiver must decline then: the user method is the likelier target. */
/* The scan below is O(nclasses * chain) per query and the fallback arms ask it
   per call site per fixpoint iteration; memoize per name. The answer depends
   only on scope/class shape, so stamp entries with the scope-index epoch (the
   same invalidation hash_new_default_arg's memo rides) plus the scope/class
   counts for the unfrozen phases where the epoch doesn't tick. */
static unsigned udm_gen = 0;
static int udm_nscopes = -1, udm_nclasses = -1, udm_cap = 0, udm_n = 0;
static char **udm_names = NULL;
static signed char *udm_ans = NULL;
static int *udm_next = NULL, *udm_head = NULL;
static unsigned udm_hash(const char *s) {
  unsigned h = 2166136261u;
  for (const char *p = s; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
  return h;
}
/* Per-epoch string sets making the memo fill O(1) for the common case:
   every instance-method name any class defines, and every alias source name.
   Open-addressed; rebuilt alongside the memo on an epoch change. */
typedef struct { char **v; int n, cap; } UdmSet;
static UdmSet udm_defined_set, udm_aliased_set;
static int udm_sets_stale = 1;
static void udm_set_clear(UdmSet *s) {
  for (int i = 0; i < s->cap; i++) { free(s->v[i]); s->v[i] = NULL; }
  s->n = 0;
}
static void udm_set_add(UdmSet *s, const char *k) {
  if (!k) return;
  if (s->n * 2 >= s->cap) {
    int ncap = s->cap ? s->cap * 2 : 1024;
    char **nv = calloc((size_t)ncap, sizeof(char *));
    if (!nv) return;
    for (int i = 0; i < s->cap; i++)
      if (s->v[i]) {
        unsigned j = udm_hash(s->v[i]) & (unsigned)(ncap - 1);
        while (nv[j]) j = (j + 1) & (unsigned)(ncap - 1);
        nv[j] = s->v[i];
      }
    free(s->v); s->v = nv; s->cap = ncap;
  }
  unsigned j = udm_hash(k) & (unsigned)(s->cap - 1);
  while (s->v[j]) {
    if (sp_streq(s->v[j], k)) return;
    j = (j + 1) & (unsigned)(s->cap - 1);
  }
  s->v[j] = strdup(k);
  if (s->v[j]) s->n++;
}
static int udm_set_has(const UdmSet *s, const char *k) {
  if (!s->cap) return 0;
  unsigned j = udm_hash(k) & (unsigned)(s->cap - 1);
  while (s->v[j]) {
    if (sp_streq(s->v[j], k)) return 1;
    j = (j + 1) & (unsigned)(s->cap - 1);
  }
  return 0;
}
static void udm_sets_fill(Compiler *c) {
  udm_set_clear(&udm_defined_set);
  udm_set_clear(&udm_aliased_set);
  for (int s = 0; s < c->nscopes; s++)
    if (c->scopes[s].class_id >= 0 && !c->scopes[s].is_cmethod && c->scopes[s].name)
      udm_set_add(&udm_defined_set, c->scopes[s].name);
  for (int k = 0; k < c->nclasses; k++)
    for (int i = 0; i < c->classes[k].naliases; i++)
      udm_set_add(&udm_aliased_set, c->classes[k].alias_new[i]);
  udm_sets_stale = 0;
}
static int udm_defined_name(Compiler *c, const char *name) {
  if (udm_sets_stale) udm_sets_fill(c);
  return udm_set_has(&udm_defined_set, name);
}
static int udm_aliased_name(Compiler *c, const char *name) {
  if (udm_sets_stale) udm_sets_fill(c);
  return udm_set_has(&udm_aliased_set, name);
}
int an_user_defines_method(Compiler *c, const char *name) {
  if (!name) return 0;
  if (!comp_scope_index_is_frozen()) {
    /* scope shape may still change without the counts moving (renames);
       don't consult or populate the memo outside the frozen fixpoint. */
    for (int uk = 0; uk < c->nclasses; uk++)
      if (comp_method_in_chain(c, uk, name, NULL) >= 0) return 1;
    return comp_method_index(c, name) >= 0;
  }
  unsigned gen = comp_scope_index_gen();
  if (gen != udm_gen || c->nscopes != udm_nscopes || c->nclasses != udm_nclasses) {
    for (int i = 0; i < udm_n; i++) free(udm_names[i]);
    udm_n = 0;
    if (!udm_cap) {
      udm_cap = 4096;
      udm_names = malloc(sizeof(char *) * (size_t)udm_cap);
      udm_ans = malloc((size_t)udm_cap);
      udm_next = malloc(sizeof(int) * (size_t)udm_cap);
      udm_head = malloc(sizeof(int) * (size_t)udm_cap);
      if (!udm_names || !udm_ans || !udm_next || !udm_head) { udm_cap = 0; }
    }
    for (int i = 0; i < udm_cap; i++) udm_head[i] = -1;
    udm_gen = gen; udm_nscopes = c->nscopes; udm_nclasses = c->nclasses;
    udm_sets_stale = 1;
  }
  unsigned b = udm_cap ? udm_hash(name) & (unsigned)(udm_cap - 1) : 0;
  if (udm_cap)
    for (int i = udm_head[b]; i >= 0; i = udm_next[i])
      if (sp_streq(udm_names[i], name)) return udm_ans[i];
  int ans = 0;
  if (udm_aliased_name(c, name)) {
    /* the name is an alias source somewhere: per-class resolution can redirect
       it, so only the full scan answers exactly (rare -- memoized above) */
    for (int uk = 0; uk < c->nclasses; uk++)
      if (comp_method_in_chain(c, uk, name, NULL) >= 0) { ans = 1; break; }
  } else {
    /* no alias redirects this name anywhere, so resolution is the identity for
       every class: defined iff some class's own method table has it (take that
       class as the chain start) */
    ans = udm_defined_name(c, name);
  }
  if (!ans) ans = comp_method_index(c, name) >= 0;
  if (udm_cap && udm_n < udm_cap) {
    udm_names[udm_n] = strdup(name);
    if (udm_names[udm_n]) {
      udm_ans[udm_n] = (signed char)ans;
      udm_next[udm_n] = udm_head[b]; udm_head[b] = udm_n; udm_n++;
    }
  }
  return ans;
}

TyKind yield_aware_elem_ty(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  int n = node;
  while (n >= 0 && nt_type(nt, n) && sp_streq(nt_type(nt, n), "ParenthesesNode")) {
    int body = nt_ref(nt, n, "body"); int bn = 0;
    const int *bd = body >= 0 ? nt_arr(nt, body, "body", &bn) : NULL;
    n = bn == 1 ? bd[0] : -1;
  }
  if (n >= 0 && nt_type(nt, n) && sp_streq(nt_type(nt, n), "YieldNode")) {
    Scope *sc = comp_scope_of(c, n);
    int mi = sc ? (int)(sc - c->scopes) : -1;
    if (mi >= 0 && yield_value_diverges(c, mi)) return TY_POLY;
  }
  return infer_type(c, node);
}

TyKind method_call_ret(Compiler *c, int mi, int call_id) {
  int last = scope_body_last(c, mi);
  int is_yield = last >= 0 && nt_type(c->nt, last) && sp_streq(nt_type(c->nt, last), "YieldNode");
  /* A force-lowered Enumerable #each returns self (its ret is pinned to the
     defining class), not the block's value -- the per-call-site block typing
     below is only for the value-carrying (self-recursive) lowering. */
  if (c->scopes[mi].is_lowered_yield && ty_is_object(c->scopes[mi].ret))
    return c->scopes[mi].ret;
  /* Lowered because a yield sits in a lifted Thread/Fiber body: the method's
     value is its own tail (`t.value`), not the block's (#3355). */
  if (c->scopes[mi].lowered_lifted_yield) return c->scopes[mi].ret;
  /* Lowered yield methods (self-recursive + yield) carry the block's return value:
     return the per-call-site block body type so puts/assign use the right type. */
  if (c->scopes[mi].is_lowered_yield || is_yield || is_blk_param_call(c, last, mi)) {
    int blk = nt_ref(c->nt, call_id, "block");
    const char *bty = blk >= 0 ? nt_type(c->nt, blk) : NULL;
    /* `callee(&b)` / `callee(...)` forwards the block active in the enclosing
       method (a `...` forward carries it implicitly, with no block node), so
       the value `callee` yields is whatever that forwarded block produces --
       i.e. the enclosing method's own per-call-site yield value. */
    int fwd = (bty && sp_streq(bty, "BlockArgumentNode"));
    if (!fwd && blk < 0) {
      int a = nt_ref(c->nt, call_id, "arguments");
      int an = 0; const int *av = a >= 0 ? nt_arr(c->nt, a, "arguments", &an) : NULL;
      fwd = (an == 1 && av && nt_type(c->nt, av[0]) &&
             sp_streq(nt_type(c->nt, av[0]), "ForwardingArgumentsNode"));
    }
    if (fwd) {
      Scope *encl = comp_scope_of(c, call_id);
      int emi = encl ? (int)(encl - c->scopes) : -1;
      if (emi >= 0 && emi != mi) {
        TyKind ft = yield_value_type(c, emi);
        if (ft != TY_UNKNOWN && ft != TY_VOID) return ft;
      }
    }
    if (blk >= 0) {
      int bbody = nt_ref(c->nt, blk, "body");
      int bn = 0; const int *bb = bbody >= 0 ? nt_arr(c->nt, bbody, "body", &bn) : NULL;
      if (bn > 0) {
        const char *lty = nt_type(c->nt, bb[bn - 1]);
        if (lty && sp_streq(lty, "ReturnNode"))
          return return_node_type(c, bb[bn - 1]);  /* `{ return e }`: see yield_value_type */
        return infer_type(c, bb[bn - 1]);
      }
    }
  }
  return c->scopes[mi].ret;
}
int is_proc_constant(const NodeTable *nt, int n) {
  if (n < 0) return 0;
  const char *ty = nt_type(nt, n);
  if (!ty) return 0;
  if (sp_streq(ty, "ConstantReadNode") || sp_streq(ty, "ConstantPathNode")) {
    const char *nm = nt_str(nt, n, "name");
    return nm && sp_streq(nm, "Proc");
  }
  return 0;
}
int is_proc_literal(Compiler *c, int id) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, id);
  if (!ty || !sp_streq(ty, "CallNode")) return 0;
  if (nt_ref(nt, id, "block") < 0) return 0;
  const char *name = nt_str(nt, id, "name");
  int recv = nt_ref(nt, id, "receiver");
  if (recv < 0 && name && (sp_streq(name, "proc") || sp_streq(name, "lambda"))) return 1;
  if (recv >= 0 && name && sp_streq(name, "new") && is_proc_constant(nt, recv)) return 1;
  return 0;
}
/* A block lowered to a REAL sp_Proc by its consumer -- a deferred handler
   (at_exit / Signal.trap, #2836) or an ENV block mutator (#2832). These need
   the full proc capture machinery but keep their own call-level inference
   (trap returns the previous handler, not the proc). */
int is_handler_proc_block(Compiler *c, int id) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, id);
  if (!ty || !sp_streq(ty, "CallNode")) return 0;
  if (nt_ref(nt, id, "block") < 0) return 0;
  const char *name = nt_str(nt, id, "name");
  int recv = nt_ref(nt, id, "receiver");
  if (!name) return 0;
  if (sp_streq(name, "at_exit") || sp_streq(name, "trap")) {
    if (recv < 0) return 1;
    const char *hrty = nt_type(nt, recv);
    return hrty && sp_streq(hrty, "ConstantReadNode") &&
           nt_str(nt, recv, "name") && sp_streq(nt_str(nt, recv, "name"), "Signal");
  }
  if (recv >= 0 && nt_kind(nt, recv) == NK_ConstantReadNode &&
      nt_str(nt, recv, "name") && sp_streq(nt_str(nt, recv, "name"), "ENV") &&
      (sp_streq(name, "delete_if") || sp_streq(name, "reject!") ||
       sp_streq(name, "keep_if") || sp_streq(name, "select!") ||
       sp_streq(name, "filter!")))
    return 1;
  return 0;
}
int is_proc_create(Compiler *c, int id) {
  const char *ty = nt_type(c->nt, id);
  if (ty && sp_streq(ty, "LambdaNode")) return 1;
  return is_proc_literal(c, id);
}
/* Unify the value types of `return <expr>` nodes lexically inside a proc body,
   without descending into a nested scope (a def/class/module, or a nested block
   or lambda -- whose returns are their own or non-local). Used to widen a proc
   whose paths return different types to TY_POLY, so the .call site reads the
   boxed return slot instead of trusting one path's scalar type. */
static TyKind proc_interior_return_ty(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, node);
  if (!ty) return TY_UNKNOWN;
  if (sp_streq(ty, "ReturnNode")) return return_node_type(c, node);
  /* A nested def/class/module/lambda owns its own returns; stop there. A plain
     block is return-transparent -- a `return` lexically inside a `do..end`
     block returns non-locally from the enclosing proc/lambda, so its type must
     fold in (#3241). A block that IS a nested proc literal's block owns its own
     frame, so it is not descended into. */
  if (sp_streq(ty, "DefNode") || sp_streq(ty, "ClassNode") ||
      sp_streq(ty, "ModuleNode") || sp_streq(ty, "LambdaNode")) return TY_UNKNOWN;
  TyKind r = TY_UNKNOWN;
  int nr = nt_num_refs(nt, node);
  for (int i = 0; i < nr; i++) {
    int ch = nt_ref_at(nt, node, i);
    if (ch >= 0 && nt_type(nt, ch) && sp_streq(nt_type(nt, ch), "BlockNode") &&
        is_proc_literal(c, node)) continue;   /* a nested proc's own block */
    TyKind s = proc_interior_return_ty(c, ch);
    if (s != TY_UNKNOWN) r = (r == TY_UNKNOWN) ? s : ty_unify(r, s);
  }
  int na = nt_num_arrs(nt, node);
  for (int i = 0; i < na; i++) {
    int n = 0; const int *ids = nt_arr_at(nt, node, i, &n);
    for (int k = 0; k < n; k++) {
      TyKind s = proc_interior_return_ty(c, ids[k]);
      if (s != TY_UNKNOWN) r = (r == TY_UNKNOWN) ? s : ty_unify(r, s);
    }
  }
  return r;
}
TyKind proc_node_ret(Compiler *c, int create) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, create);
  int body;
  if (ty && sp_streq(ty, "LambdaNode")) body = nt_ref(nt, create, "body");
  else {
    int blk = nt_ref(nt, create, "block");
    /* `Proc.new(&b)` / `proc(&b)`: no body of its own -- the value is the
       forwarded proc, whose return is unknowable here (a block param's
       actual block lives at the caller). TY_UNKNOWN maps to the boxed poly
       return at the .call site instead of a silent nil. */
    if (blk >= 0 && nt_type(nt, blk) && sp_streq(nt_type(nt, blk), "BlockArgumentNode"))
      return TY_UNKNOWN;
    body = blk >= 0 ? nt_ref(nt, blk, "body") : -1;
  }
  if (body < 0) return TY_NIL;
  int bn = 0;
  const int *bb = nt_arr(nt, body, "body", &bn);
  if (bn <= 0) return TY_NIL;
  /* An explicit `return <expr>` tail is a ReturnNode, which infer_type does not
     value-type -- type the proc by the returned expression so the call site and
     the body's return channel agree (otherwise the value is silently lost). */
  int tail = bb[bn - 1];
  const char *tty = nt_type(nt, tail);
  TyKind tail_ty = (tty && sp_streq(tty, "ReturnNode")) ? return_node_type(c, tail)
                                                        : infer_type(c, tail);
  /* Widen to the unification of every exit's type: a proc returning different
     types on different paths (`return "x" if c; n`) must be POLY so the .call
     site reads the boxed slot rather than mis-unboxing one path's scalar type
     (silent-wrong under the universal boxed return ABI). A block-level `next`/
     `break <expr>` carries a value out of the proc too (loop-bound ones bind to
     the loop, not the proc -- ie_block_break_next_ty stops at nested loops and
     scopes), so fold their types in as well. */
  TyKind ir = proc_interior_return_ty(c, body);
  TyKind bnt = ie_block_break_next_ty(c, body);
  if (bnt != TY_UNKNOWN) ir = (ir == TY_UNKNOWN) ? bnt : ty_unify(ir, bnt);
  if (ir != TY_UNKNOWN && tail_ty != TY_UNKNOWN && ir != tail_ty)
    return ty_unify(tail_ty, ir);
  return tail_ty;
}
TyKind proc_ret_of(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  const char *ty = nt_type(nt, node);
  if (!ty) return TY_UNKNOWN;
  /* unwrap `(expr)` so `(f << g).call` sees the composition node */
  if (sp_streq(ty, "ParenthesesNode")) {
    int body = nt_ref(nt, node, "body");
    int bn = 0; const int *bb = body >= 0 ? nt_arr(nt, body, "body", &bn) : NULL;
    return bn == 1 ? proc_ret_of(c, bb[0]) : TY_UNKNOWN;
  }
  if (sp_streq(ty, "LambdaNode") || is_proc_literal(c, node)) return proc_node_ret(c, node);
  if (sp_streq(ty, "LocalVariableReadNode")) {
    Scope *s = comp_scope_of(c, node);
    LocalVar *lv = scope_local(s, nt_str(nt, node, "name"));
    return lv ? (TyKind)lv->proc_ret : TY_UNKNOWN;
  }
  if (sp_streq(ty, "CallNode")) {
    /* a method call that returns a proc -> the callee's recorded proc return */
    int recv = nt_ref(nt, node, "receiver");
    const char *name = nt_str(nt, node, "name");
    /* Hash#to_proc: the proc maps a key to the hash's value type. */
    if (recv >= 0 && name && sp_streq(name, "to_proc")) {
      TyKind rt = infer_type(c, recv);
      if (ty_is_hash(rt)) return ty_hash_val(rt);
    }
    /* proc << proc / proc >> proc: the composed call returns the OUTER proc's
       value (f<<g outer=f; f>>g outer=g). */
    if (recv >= 0 && name && infer_type(c, recv) == TY_PROC) {
      int args = nt_ref(nt, node, "arguments");
      int an = 0; const int *av = args >= 0 ? nt_arr(nt, args, "arguments", &an) : NULL;
      if (an == 1 && infer_type(c, av[0]) == TY_PROC) {
        if (sp_streq(name, "<<")) return proc_ret_of(c, recv);
        if (sp_streq(name, ">>")) return proc_ret_of(c, av[0]);
      }
    }
    int mi = -1;
    if (recv < 0) {
      mi = comp_method_index(c, name);
      if (mi < 0) { Scope *self = comp_scope_of(c, node); if (self->class_id >= 0) mi = comp_method_in_chain(c, self->class_id, name, NULL); }
    }
else {
      TyKind rt = infer_type(c, recv);
      if (ty_is_object(rt)) mi = comp_method_in_chain(c, ty_object_class(rt), name, NULL);
    }
    if (mi >= 0) return (TyKind)c->scopes[mi].ret_proc_ret;
  }
  return TY_UNKNOWN;
}
TyKind proc_call_ret(Compiler *c, int recv) {
  TyKind r = proc_ret_of(c, recv);
  return r == TY_UNKNOWN ? TY_POLY : r;
}

/* The symbol-name argument of a `method(:sym)` call, or NULL. */
const char *method_sym_arg(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  /* <method>.super_method names the same method (in the parent class) */
  if (node >= 0 && nt_kind(nt, node) == NK_CallNode && nt_str(nt, node, "name") &&
      sp_streq(nt_str(nt, node, "name"), "super_method")) {
    int imn = method_recv_node(c, nt_ref(nt, node, "receiver"));
    return imn >= 0 ? method_sym_arg(c, imn) : NULL;
  }
  int args = nt_ref(nt, node, "arguments");
  int an = 0; const int *av = args >= 0 ? nt_arr(nt, args, "arguments", &an) : NULL;
  if (an < 1) return NULL;
  const char *aty = nt_type(nt, av[0]);
  if (aty && sp_streq(aty, "SymbolNode")) return nt_str(nt, av[0], "value");
  if (aty && sp_streq(aty, "StringNode")) {
    const char *s = nt_str(nt, av[0], "content");
    return s ? s : nt_str(nt, av[0], "unescaped");
  }
  return NULL;
}

/* True if `node` is a `method(:sym)` / `<recv>.method(:sym)` call. */
int is_method_obj_call(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  if (node < 0 || !nt_type(nt, node) || !sp_streq(nt_type(nt, node), "CallNode")) return 0;
  const char *nm = nt_str(nt, node, "name");
  /* Module#instance_method builds the same object, unbound (NULL self); the
     Method machinery (name/arity/owner/call-after-bind) rides it (#2676) */
  return nm && (sp_streq(nm, "method") || sp_streq(nm, "instance_method")) &&
         method_sym_arg(c, node) != NULL;
}

/* The target method scope index bound by a `method(:sym)` node, or -1
   (e.g. a top-level Kernel method like `puts`, or a builtin-array receiver). */
int method_obj_target_mi(Compiler *c, int node) {
  const NodeTable *nt = c->nt;
  /* <method>.super_method: the same-named method one step up the defining
     class's ancestor chain, or -1 (nil) when there is none (#3247). */
  if (node >= 0 && nt_kind(nt, node) == NK_CallNode && nt_str(nt, node, "name") &&
      sp_streq(nt_str(nt, node, "name"), "super_method")) {
    int imn = method_recv_node(c, nt_ref(nt, node, "receiver"));
    int imi = imn >= 0 ? method_obj_target_mi(c, imn) : -1;
    if (imi < 0 || c->scopes[imi].class_id < 0) return -1;
    int par = c->classes[c->scopes[imi].class_id].parent;
    if (par < 0) return -1;
    return c->scopes[imi].is_cmethod
             ? comp_cmethod_in_chain(c, par, c->scopes[imi].name, NULL)
             : comp_method_in_chain(c, par, c->scopes[imi].name, NULL);
  }
  const char *sym = method_sym_arg(c, node);
  if (!sym) return -1;
  int recv = nt_ref(nt, node, "receiver");
  /* Klass.instance_method(:m): the class's INSTANCE method (Klass.method(:m)
     would be the class method) */
  {
    const char *nm0 = nt_str(nt, node, "name");
    if (nm0 && sp_streq(nm0, "instance_method")) {
      const char *rn = (recv >= 0 && nt_kind(nt, recv) == NK_ConstantReadNode)
                       ? nt_str(nt, recv, "name") : NULL;
      int ci = rn ? comp_class_index(c, rn) : -1;
      return ci >= 0 ? comp_method_in_chain(c, ci, sym, NULL) : -1;
    }
  }
  if (recv < 0) {
    int mi = comp_method_index(c, sym);
    if (mi < 0) { Scope *s = comp_scope_of(c, node); if (s && s->class_id >= 0) mi = comp_method_in_chain(c, s->class_id, sym, NULL); }
    return mi;
  }
  TyKind rt = infer_type(c, recv);
  /* a receiver whose method() was retargeted at a synthesized __bam_* wrapper
     (desugar_builtin_method_obj) -- for an object accessor the wrapper is a
     TOP-LEVEL def, not in the object's own class chain, so resolve it there. */
  if (sym[0] == '_' && sym[1] == '_' && sym[2] == 'b' && sym[3] == 'a' && sym[4] == 'm')
    return comp_method_index(c, sym);
  if (ty_is_object(rt)) return comp_method_in_chain(c, ty_object_class(rt), sym, NULL);
  /* Klass.method(:cmeth) / Module.method(:mf): the class-side method */
  if (rt == TY_CLASS && nt_kind(nt, recv) == NK_ConstantReadNode) {
    const char *rn2 = nt_str(nt, recv, "name");
    int ci2 = rn2 ? comp_class_index(c, rn2) : -1;
    if (ci2 >= 0) return comp_cmethod_in_chain(c, ci2, sym, NULL);
  }
  return -1;
}

/* Is this Method-typed expression an UNBOUND method (Klass.instance_method
   with no #bind crossed)? Resolves the same way method_recv_node does, but a
   bind on the path means the value is bound (#2724). */
int method_expr_is_unbound(Compiler *c, int recv) {
  const NodeTable *nt = c->nt;
  if (recv < 0) return 0;
  if (nt_kind(nt, recv) == NK_CallNode) {
    const char *nm = nt_str(nt, recv, "name");
    if (nm && sp_streq(nm, "bind")) return 0;
    if (nm && sp_streq(nm, "instance_method") && method_sym_arg(c, recv) != NULL) return 1;
    if (nm && sp_streq(nm, "unbind")) return 1;
    if (nm && sp_streq(nm, "super_method"))
      return method_expr_is_unbound(c, nt_ref(nt, recv, "receiver"));
    return 0;
  }
  if (nt_kind(nt, recv) == NK_LocalVariableReadNode) {
    const char *vn = nt_str(nt, recv, "name");
    Scope *sc = vn ? comp_scope_of(c, recv) : NULL;
    int found = 0, n = 0;
    for (int w = 0; vn && w < nt->count; w++) {
      if (nt_kind(nt, w) != NK_LocalVariableWriteNode) continue;
      const char *wn = nt_str(nt, w, "name");
      if (!wn || !sp_streq(wn, vn) || comp_scope_of(c, w) != sc) continue;
      n++;
      if (method_expr_is_unbound(c, nt_ref(nt, w, "value"))) found = 1;
    }
    return n == 1 && found;   /* a re-written local is dynamic: stay bound-ish */
  }
  return 0;
}

/* The `method(:sym)` node a Method-typed expression resolves to: either the
   call itself (inline) or, for a local variable, its assignment in scope. */
int method_recv_node(Compiler *c, int recv) {
  const NodeTable *nt = c->nt;
  if (recv < 0) return -1;
  if (is_method_obj_call(c, recv)) return recv;
  /* UnboundMethod#bind(obj) re-binds the same target: see through it (#2676).
     dup/clone of a method are identity copies: see through them too (#3247). */
  if (nt_kind(nt, recv) == NK_CallNode && nt_str(nt, recv, "name") &&
      (sp_streq(nt_str(nt, recv, "name"), "bind") ||
       sp_streq(nt_str(nt, recv, "name"), "dup") ||
       sp_streq(nt_str(nt, recv, "name"), "clone")))
    return method_recv_node(c, nt_ref(nt, recv, "receiver"));
  /* a super_method call IS a method node: method_obj_target_mi resolves it
     through the parent chain, so hand it back as-is (#3247) */
  if (nt_kind(nt, recv) == NK_CallNode && nt_str(nt, recv, "name") &&
      sp_streq(nt_str(nt, recv, "name"), "super_method") &&
      method_recv_node(c, nt_ref(nt, recv, "receiver")) >= 0)
    return recv;
  const char *rty = nt_type(nt, recv);
  if (rty && sp_streq(rty, "LocalVariableReadNode")) {
    const char *vn = nt_str(nt, recv, "name");
    Scope *sc = comp_scope_of(c, recv);
    for (int w = 0; w < nt->count; w++) {
      const char *wty = nt_type(nt, w);
      if (!wty || !sp_streq(wty, "LocalVariableWriteNode")) continue;
      if (comp_scope_of(c, w) != sc) continue;
      const char *wn = nt_str(nt, w, "name");
      if (!wn || !vn || !sp_streq(wn, vn)) continue;
      int val = nt_ref(nt, w, "value");
      /* resolve the written expression with the same rules as a direct
         receiver (sees through bind/dup/clone chains, super_method) */
      int inner = method_recv_node(c, val);
      if (inner >= 0) return inner;
    }
  }
  return -1;
}

/* The `method(:sym)` node behind a Proc-typed expression created by
   `<method>.to_proc`: the to_proc call itself, or a local variable's
   single assignment to one. Returns -1 when the proc has another origin. */
int proc_to_proc_method_node(Compiler *c, int recv) {
  const NodeTable *nt = c->nt;
  if (recv < 0) return -1;
  int cand = recv;
  const char *rty = nt_type(nt, recv);
  if (rty && sp_streq(rty, "LocalVariableReadNode")) {
    const char *vn = nt_str(nt, recv, "name");
    Scope *sc = comp_scope_of(c, recv);
    cand = -1;
    for (int w = 0; w < nt->count; w++) {
      const char *wty = nt_type(nt, w);
      if (!wty || !sp_streq(wty, "LocalVariableWriteNode")) continue;
      if (comp_scope_of(c, w) != sc) continue;
      const char *wn = nt_str(nt, w, "name");
      if (!wn || !vn || !sp_streq(wn, vn)) continue;
      int val = nt_ref(nt, w, "value");
      const char *vty = val >= 0 ? nt_type(nt, val) : NULL;
      if (vty && sp_streq(vty, "CallNode")) { cand = val; break; }
    }
  }
  if (cand < 0) return -1;
  const char *cty = nt_type(nt, cand);
  if (!cty || !sp_streq(cty, "CallNode")) return -1;
  const char *nm = nt_str(nt, cand, "name");
  if (!nm || !sp_streq(nm, "to_proc")) return -1;
  return method_recv_node(c, nt_ref(nt, cand, "receiver"));
}

/* Param-index shift for a call through a Method object. A bound
   `<recv>.method(:__bam_N)` resolves to a synthesized top-level wrapper whose
   first param (__bam_r) is carried by the Method's self slot, so positional
   call args map to params[1..]. A real instance/class method keeps self
   implicit (params are the declared ones) and shifts by 0. */
int method_call_param_shift(Compiler *c, int mn, int mi) {
  if (mn < 0 || mi < 0) return 0;
  if (nt_ref(c->nt, mn, "receiver") < 0) return 0;
  Scope *m = &c->scopes[mi];
  return (m->class_id < 0 && !m->is_cmethod) ? 1 : 0;
}

/* True when scope `scope_idx` contains an explicit `return` (such a method
   cannot be inlined at its call sites). Shared by the inliner and the
   valued-break detector. */
int scope_has_return(Compiler *c, int scope_idx) {
  for (int id = 0; id < c->nt->count; id++) {
    const char *ty = nt_type(c->nt, id);
    if (ty && sp_streq(ty, "ReturnNode") && c->nscope[id] == scope_idx) return 1;
  }
  return 0;
}

/* Resolve a block-bearing CallNode to an INLINE-ABLE yielding user method:
   mirrors emit_inline_call_x's resolution (free function -> implicit-self
   chain -> Cls class method -> object-receiver chain) and its
   yields/!return guard. -1 for anything else -- builtin iterators, `loop`,
   `catch`, proc/lambda literals, and methods the inliner would refuse. */
int call_user_yield_mi(Compiler *c, int id) {
  const NodeTable *nt = c->nt;
  const char *name = nt_str(nt, id, "name");
  int recv = nt_ref(nt, id, "receiver");
  if (!name) return -1;
  int mi = -1;
  if (recv < 0) {
    mi = comp_method_index(c, name);
    if (mi < 0) {
      Scope *encl = comp_scope_of(c, id);
      if (encl && encl->class_id >= 0) {
        mi = comp_method_in_chain(c, encl->class_id, name, NULL);
        /* inside a class method, a bare call also reaches sibling class
           methods (self is the class there) */
        if (mi < 0 && encl->is_cmethod)
          mi = comp_cmethod_in_chain(c, encl->class_id, name, NULL);
      }
    }
  }
  else {
    TyKind rt = infer_type(c, recv);
    const char *rty = nt_type(nt, recv);
    /* a scoped receiver (NS::Base.transaction { }) resolves by its leaf name,
       the key classes are indexed under */
    const char *cname = (rty && (sp_streq(rty, "ConstantReadNode") ||
                                 sp_streq(rty, "ConstantPathNode")))
                        ? nt_str(nt, recv, "name") : NULL;
    int ci = cname ? comp_class_index(c, cname) : -1;
    if (ci >= 0) mi = comp_cmethod_in_chain(c, ci, name, NULL);
    else if (ty_is_object(rt)) mi = comp_method_in_chain(c, ty_object_class(rt), name, NULL);
  }
  if (mi < 0) return -1;
  Scope *m = &c->scopes[mi];
  if (!m->yields || scope_has_return(c, mi)) return -1;
  return mi;
}

/* The RangeNode behind a TY_RANGE local-variable read, when the local has
   exactly one write anywhere in the program (a plain LocalVariableWriteNode
   in the read's own scope) and that write's value is a (possibly
   parenthesized) range literal. Lets endpoint-shape checks (endless /
   Float::INFINITY size, take/first prefix) see through the local, e.g.
   `r = (1..Float::INFINITY); r.size`. Same-name writes elsewhere -- other
   scopes, destructures, or/and/op-assigns -- disqualify conservatively
   (there is no depth field to tell a captured outer write apart).
   Returns -1 when the receiver has any other origin. */
int local_sole_range_node(Compiler *c, int recv) {
  const NodeTable *nt = c->nt;
  if (recv < 0) return -1;
  const char *rty = nt_type(nt, recv);
  if (!rty || !sp_streq(rty, "LocalVariableReadNode")) return -1;
  const char *vn = nt_str(nt, recv, "name");
  if (!vn) return -1;
  Scope *sc = comp_scope_of(c, recv);
  int val = -1;
  for (int w = 0; w < nt->count; w++) {
    NodeKind k = nt_kind(nt, w);
    if (k != NK_LocalVariableWriteNode && k != NK_LocalVariableOrWriteNode &&
        k != NK_LocalVariableAndWriteNode && k != NK_LocalVariableOperatorWriteNode &&
        k != NK_LocalVariableTargetNode)
      continue;
    const char *wn = nt_str(nt, w, "name");
    if (!wn || !sp_streq(wn, vn)) continue;
    if (k != NK_LocalVariableWriteNode || comp_scope_of(c, w) != sc || val >= 0)
      return -1;
    val = nt_ref(nt, w, "value");
  }
  while (val >= 0 && nt_kind(nt, val) == NK_ParenthesesNode) {
    int pb = nt_ref(nt, val, "body");
    int pn = 0;
    const int *pp = pb >= 0 ? nt_arr(nt, pb, "body", &pn) : NULL;
    val = pn == 1 ? pp[0] : -1;
  }
  if (val < 0 || nt_kind(nt, val) != NK_RangeNode) return -1;
  /* callers re-emit the endpoints at the use site (take/first prefix loops),
     so both must be pure: literals or constant reads only */
  for (int e = 0; e < 2; e++) {
    int ep = nt_ref(nt, val, e ? "right" : "left");
    if (ep < 0) continue;
    NodeKind ek = nt_kind(nt, ep);
    if (ek != NK_IntegerNode && ek != NK_FloatNode && ek != NK_NilNode &&
        ek != NK_ConstantReadNode && ek != NK_ConstantPathNode)
      return -1;
  }
  return val;
}
