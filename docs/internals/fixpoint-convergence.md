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
never settle. Three separate rounds of this accounted for 78 of the 91 capped
programs in the suite:

- eight sites in `infer_write_types` and four in `infer_case_pattern_locals`
  (d4fe2ba5)
- `pm_seed_locals_poly`'s leaf and two array-pattern bindings, reached through
  `changed |=` rather than written inline (a2f937e6)
- the end-of-pass comparison ran over slots the reset had SKIPPED
  (`rbs_seeded`, which also marks a desugar-synthesized temp), so their
  `gc_root` was a 0 nothing had written (10b71874)

So: **inside that window, only the end-of-pass sweep reports.** Ivars, class
variables, constants and parameters are not reset, so their sites report
normally.

A promotion the reset wipes has to be re-asserted inside the window, the way
`oa_pin` and the shared-string marks are (92ce0476), or the pass that made it
will keep re-making it forever.

## What is left

Ten programs still cap, in four genuine oscillations. None is the shape above;
each is two passes that disagree, or one pass that cannot re-derive what
another can.

| slot moves | programs | between |
|---|---|---|
| `strbuf -> poly` | `bang_result_through_shared_handle`, `poly_string_append_integer`, `string_alias_out_of_container` | `infer_write_types` widens to poly, `promote_shared_stored_strings` forces `TY_STRBUF` back with no type guard. Re-asserting strbuf for a POLY slot converges these three and starts three others oscillating, so the open question is whether a slot that has widened past String is still a shared string at all. |
| `sym_poly_hash -> unknown` | `hash_conformance_batch7`, `hash_default_proc_key`, `issue_3288`, `nil_hashdefault_complex_followups` | `infer_write_types` cannot derive `g = Hash.new(99)` (the RHS reads UNKNOWN there) and leaves the slot cleared; `desugar_enum_method_recv`'s `infer_type` calls settle it again later in the round. A derivation gap, surfacing as an oscillation because the reset wipes the answer. |
| assorted | `issue_3196`, `nil_receiver_tap_then`, `unresolved_tail_call_value` | not yet attributed |

`nil_receiver_tap_then.rb` is three lines and is the cheapest way in.

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
