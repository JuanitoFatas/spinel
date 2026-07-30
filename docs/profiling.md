# Profiling a Spinel program

Two measurements, both opt-in and both usable on a normal build: where the time
goes, and where the allocations come from. Neither needs a special compiler and
neither costs anything when it is off.

## Where the time goes: `--profile`

```
spinel app.rb --profile -o app
perf record -g ./app
perf report
```

`--profile` builds the same `-O2` binary the default build produces, plus the
three things a sampling profiler needs: `-g`, `-fno-omit-frame-pointer`, and an
unstripped symbol table. It also writes `app.symbols.json` beside the binary —
the `--emit-symbol-map` payload, mapping each emitted C symbol back to the Ruby
name it came from (`sp_PPU_render_pixel` → `Optcarrot::PPU#render_pixel`).

Methods compile to `static` C functions, so a stack walk names them only when
the symbol table is present; that is what `--profile` keeps. If `perf` is
unavailable — `perf_event_paranoid` is locked down on many CI and hardened
hosts — any sampler that reads frame pointers works the same way.

## Where the allocations come from: `SPINEL_ALLOC_REPORT`

```
SPINEL_ALLOC_REPORT=1 ./app            # to stderr
SPINEL_ALLOC_REPORT=alloc.folded ./app # to a file
```

At exit the program dumps one line per allocated type, in the folded-stack
format flamegraph tools read, plus `# bytes` companion lines:

```
alloc;String 1100
alloc;Widget 1000
alloc;Hash(String) 1
alloc;(no-scan) 100
# bytes String 24200
```

`(no-scan)` covers objects with no pointers to trace — an Integer array, a byte
buffer — which the collector never has to walk.

Counters key on the object's GC scan callback, which is the de-facto type
identity, and are bumped inside the allocator itself. Nothing is sampled, so
two runs of the same program report the same numbers.

### Per-site attribution: `SPINEL_ALLOC_SITES`

```
SPINEL_ALLOC_REPORT=1 SPINEL_ALLOC_SITES=1 ./app
```

Adds the calling frame as an outer folded frame, so a type allocated from three
places appears three times:

```
alloc;./app(+0x2bc5) [0x5a0a1e7acbc5];(no-scan) 5
```

The site is captured as a return address on the counted path and symbolised
only at exit, so the extra cost is one stack walk per allocation and no
allocation of its own. Names resolve as far as the dynamic symbol table
reaches; a `static` method — which is how user methods compile — shows as an
address. Turn it into a name with the symbol map from `--profile`, or with
`addr2line -f -e ./app <addr>` on a build that kept its symbols.

Two caveats:

- The walk needs unwind information. A build that discards it (for instance
  linking with `--gc-sections` on a stripped binary) reports sites as absent
  and falls back to the per-type lines.
- One frame is not always the frame you want: an allocation inside an inlined
  helper is attributed to whatever the compiler left as the caller. Read the
  addresses as "the code that asked for this", not as an exact source line.

## Which one to reach for

Start with `--profile` and a sampler: it tells you which method to look at.
Reach for `SPINEL_ALLOC_REPORT` when the profile points at the collector
(`sp_gc_collect`, `sp_gc_mark_all`) or at `malloc` — then the question is not
which code is slow but which code allocates, and the counters answer that
exactly rather than statistically.
