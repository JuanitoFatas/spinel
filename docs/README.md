# Spinel documentation

User documentation for Spinel, the whole-program ahead-of-time Ruby→C compiler.
Start here, then open the topic you need.

## For users

- **[spin.md](spin.md)** — projects and packages with `spin`:
  scaffold, dependencies (path / git / index), lockfile, tests, vendoring,
  and native C in packages. Start here to build anything bigger than one file.
- **[limitations.md](limitations.md)** — what an AOT compiler can and cannot do.
  The honest catalogue of where Spinel differs from CRuby, and why. Read this
  first if something behaves unexpectedly.
- **[require.md](require.md)** — how `require` works: which stdlib needs which
  `require`, what an absent or unsatisfiable `require` does, and how to provide
  a feature of your own with `-I`.
- **[FFI.md](FFI.md)** — call C functions directly from Spinel Ruby, with no
  extension build step: declarations in the source become direct C call sites.
- **[rbs-extract.md](rbs-extract.md)** — seed the type inferencer with `.rbs`
  signatures via `spinel --rbs DIR`: the supported RBS subset, what a seed buys,
  and why a seed is an assertion you are trusted to get right rather than a hint.
- **[float-rounding.md](float-rounding.md)** — the return type of
  `Float#ceil`/`#floor`/`#round`/`#truncate`, where Spinel's static typing meets
  CRuby's value-dependent rule.
- **[int-overflow.md](int-overflow.md)** — `--int-overflow=raise|wrap|promote`:
  what happens when an `Integer` crosses Spinel's 64-bit boundary.
- **[profiling.md](profiling.md)** — where the time goes (`--profile` plus any
  frame-pointer sampler) and where the allocations come from
  (`SPINEL_ALLOC_REPORT`, with per-site attribution).
- **[thread.md](thread.md)** — `Thread` as true M:N parallelism with no GVL:
  the execution model, the supported API, and the data-race semantics that
  follow from having no global lock.

## Internals

How the compiler is built and where it is going. Not needed to *use* Spinel.

- **[internals/AST.md](internals/AST.md)** — the text AST the parser emits and
  the rest of the compiler consumes.
- **[internals/analyze-ir.md](internals/analyze-ir.md)** — the analyze ↔ codegen
  contract (the shared in-memory `Compiler` model).
- **[internals/gc.md](internals/gc.md)** — the collector: two heaps, the marker
  byte that identifies a heap string, explicit roots, a full mark with a
  generational sweep, and the limits that follow from having no write barrier.
- **[internals/thread-mn-design.md](internals/thread-mn-design.md)** — the M:N
  thread scheduler: green threads on the fiber substrate, per-worker run queues
  and work stealing, stop-the-world GC, the preemption monitor. A working
  document, not a user guarantee (the user contract is in [thread.md](thread.md)).
