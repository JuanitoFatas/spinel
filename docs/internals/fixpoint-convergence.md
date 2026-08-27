# Inference fixpoint convergence

`SP_FIXPOINT_LOG=1` prints the round count the type fixpoint reached:

```
$ SP_FIXPOINT_LOG=1 spinel -c prog.rb -o /dev/null
[fp] rounds=4
```

`rounds=128 (CAP -- did not converge)` means the loop stopped because the cap
said so, mid-oscillation. That is not only slow: **where the cap lands decides
which of two typings is emitted**, and a boxed slot prints the same answer a
typed one does, so nothing in `make test` can see which one it got.
`make infer-test` fails if `test/infer/fixpoint_converges.rb` reaches the cap.

There are two 128-round loops in `analyze_program`. The counter reports the
first; the second (the poly-local re-narrowing) converges on value stability
rather than on a no-change flag, and settles in two or three rounds. A probe
that does not separate them reads the second's deliberate per-round clearing
as the first's oscillation.

## The failure mode that is not an oscillation

`infer_write_types` clears every non-param local to `TY_UNKNOWN` at the top of
each round and re-derives it, reporting change by comparing against the type it
stashed. A site inside that window that reports `changed` **itself** is
comparing against the UNKNOWN it was just handed, so it answers "changed" every
round even when it re-derived exactly last round's answer, and the loop can
never settle. This, in five separate places, was most of it:

- eight sites in `infer_write_types` and four in `infer_case_pattern_locals`
  (d4fe2ba5)
- `pm_seed_locals_poly`'s leaf and two array-pattern bindings, reached through
  `changed |=` rather than written inline (a2f937e6) -- found by walking the
  call tree rather than the line numbers, which is what missed them the first
  time
- the end-of-pass comparison ran over slots the reset had SKIPPED
  (`rbs_seeded`, which also marks a desugar-synthesized temp such as `__ie_*`
  or `__cd_sav_*`), so their `gc_root` was a 0 nothing had written (10b71874)
- `infer_return_types` assigning `TY_POLY` and reporting unconditionally
- two arms of `infer_block_params` typing the same `tap` / `then` parameter
  differently, so the value was stable at the round's END and only the
  reporting was not (474c5ce5) -- watching the slot between passes sees
  nothing here; the attribution has to be inside the pass

So: **inside that window, only the end-of-pass sweep reports.** Ivars, class
variables, constants and parameters are not reset, so their sites report
normally.

## The three rules that came out of it

1. **A pass must be idempotent on its own output.** A fixpoint over a
   deliberately non-monotone transfer function (the reset exists so a slot can
   NARROW) terminates only if running a pass on its own result changes
   nothing. Every fix above is that property restored.

2. **A round must not discard what it could not re-derive.** A write the pass
   has no rule for (`g = Hash.new(99)`) left its slot UNKNOWN, another pass
   settled it again, and they took turns. Restoring the stash where the answer
   came out UNKNOWN cannot block a narrowing -- a narrowed slot is concrete and
   wins on its own (f00875ad). This holds for a per-round local. It does NOT
   hold for a return, which is not re-derived from scratch: refusing to lower
   one refuses a correction, and five programs stopped compiling when it was
   tried that way (93326636).

3. **A promotion the reset wipes has to be re-asserted inside the window**, the
   way `oa_pin` already is (92ce0476) -- or the pass that made it will keep
   re-making it forever. And a promotion has to stand down where its
   representation does not apply: `TY_STRBUF` is an sp_String handle, so a slot
   that widened to POLY cannot carry it, and forcing it back only fought
   whatever widened it (bc73dc5a).

## What is left

Nothing: every program under `test/` and `packages/*/test/` converges. The
sweep went 91 -> 0.

The last one was not an oscillation. `_1` .. `_9` (and `it`, which the parser
lowers to `_1`) are names spinel synthesizes rather than names the author
wrote, and blocks share their enclosing scope's local table -- so two such
blocks in one method interned the same slot and their types merged. Two passes
then typed that one slot from different call shapes, every round.

Fixed by giving each block its own (886a9924), only where the names actually
collide. What made it more than a rename is how much of the compiler treated
`_N` as something other than a parameter: `proc_param_name` answered NULL for a
numbered-param proc, so its arity was 0 and every `_1` in the body was
classified as an enclosing local and laundered through the capture machinery
rather than bound from `args[]`. Nine sites built the literal `_N`; the naming
rule now lives in one accessor.

## Measuring

```sh
for f in test/*.rb packages/*/test/*.rb; do
  r=$(SP_FIXPOINT_LOG=1 spinel -c "$f" -o /dev/null 2>&1 |
      sed -n 's/^\[fp\] rounds=\([0-9]*\).*/\1/p' | tail -1)
  echo "${r:-x} $f"
done | awk '$1==128' | wc -l
```

Convergence is bimodal: everything else in the suite settles in 2 to 11 rounds,
so "did this converge" needs no threshold to judge.

Attribution, in order: which pass still reports change at round 120+, then
which site inside it, then which slot. The first two need the probe to carry
the loop id -- the second 128-round loop clears slots on purpose, and a probe
that cannot tell them apart reads that as the first loop oscillating.
