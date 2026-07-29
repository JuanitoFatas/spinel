# RBS type seeds (`spinel --rbs`)

`spinel --rbs DIR` feeds `*.rbs` signatures from `DIR` into the type
inferencer. Under the hood the driver runs `spinel_rbs_extract`
(`tools/spinel_rbs_extract.c`) over `DIR` to turn the signatures into a
line-oriented seed file, passes it to the compiler through the
`SPINEL_RBS_SEED` environment variable, and the analyzer applies it
before the inference fixpoint.

A signature that spinel cannot represent is dropped, and dropping is
harmless: the seed line isn't emitted and the analyzer falls back to its
normal inference for that method or ivar. The rest of this document is
mostly the catalogue of what survives.

## A seed is an assertion, not a hint

A seed that IS applied is not advisory. It pins the slot, and the
widening and reset passes that make up the inference fixpoint are
forbidden to touch it. So a seed does not lose to inference; it
constrains inference from the start.

**Seeds are trusted, never verified.** No runtime check is generated. A
signature that disagrees with what the program actually stores does not
degrade to the inferred type -- it makes the emitted C reinterpret the
value:

```ruby
class C
  def set(x); @v = x; end   # stores a String
  def get;    @v;      end
end
c = C.new
c.set("hello")
p c.get
```

```rbs
class C
  attr_accessor v: Integer   # a signature the program contradicts
end
```

| build            | output              |
| ---------------- | ------------------- |
| no `--rbs`       | `"hello"`           |
| with the seed    | `99242550607929`    |
| CRuby            | `"hello"`           |

That number is the String pointer read back as an `Integer`. Nothing
warns, at compile time or at run time.

So write signatures that describe the program, not signatures you would
like to be true. A seed is closer to a `reinterpret_cast` than to a
`static_cast`.

## What a seed buys

Inference has to be conservative where two shapes meet; a seed does not.
That is the whole benefit, and it takes three distinguishable forms.

**It stops a widening the evidence forces.** In
`test/rbs-seed/pinned_container.rb`, `@kids` is only ever
lazy-initialized (`@kids ||= []`) and pushed through a reader, so the
usage pass never witnesses a direct push and the ivar widens to a boxed
value. `Array[untyped]` says nothing about the element type but does fix
the *storage kind*:

```c
// no seed
struct sp_PinBox_s { mrb_int cls_id; sp_RbVal iv_kids; sp_RbVal iv_meta; };
static sp_RbVal sp_PinBox_kids(sp_PinBox *self);
sp_poly_length(sp_PinBox_kids(...))          // runtime tag dispatch

// Array[untyped] + Hash[String, untyped]
struct sp_PinBox_s { mrb_int cls_id; sp_PolyArray * iv_kids; sp_StrPolyHash * iv_meta; };
static sp_PolyArray * sp_PinBox_kids(sp_PinBox *self);
sp_PolyArray_length(sp_PinBox_kids(...))     // direct call
```

19 `sp_poly_*` dispatching calls in that file become 6.

**It reaches slots inference has no evidence for at all.** A method with
no call sites has nothing to infer its parameters from. This is not an
optimization there; the seed is the only source of information. (The
compiler does the same thing internally: `analyze.c` pins a synthesized
wrapper's parameter with the comment "pin: no call sites exist".)

**It breaks self-referential inference.** `@data = flatten.to_a` makes
`@data`'s type depend on itself and widens it to poly. A seed cuts the
cycle.

## Ruby has one Array; spinel has several

Worth stating separately, because it is where a seed earns the most and
where a wrong one costs the most. `Array` is one type in Ruby and
`sp_IntArray` / `sp_StrArray` / `sp_PolyArray` (and the six hash
variants) in generated C. Inference picks the storage kind from
evidence; an empty `[]` or `{}` carries none.

These kinds have **different layouts**. Pinning a slot to the wrong one
is not a slower program, it is a program that reads its own data back as
garbage -- unlike, say, an object pinned to one of its ancestors, where
the field layouts coincide by construction.

## Supported

### Type vocabulary

| RBS                                    | Spinel tag                            |
| -------------------------------------- | ------------------------------------- |
| `Integer`                              | `int`                                 |
| `Float`                                | `float`                               |
| `String`                               | `string`                              |
| `Symbol`                               | `symbol`                              |
| `TrueClass`, `FalseClass`              | `bool`                                |
| `NilClass`, `nil`                      | `nil`                                 |
| `bool`                                 | `bool`                                |
| `void` (return only)                   | `nil`                                 |
| `Foo`, `Foo::Bar` (nominal)            | `obj_Foo`, `obj_Foo_Bar`              |
| `Array[Integer]`                       | `int_array`                           |
| `Array[Float]`                         | `float_array`                         |
| `Array[String]`                        | `str_array`                           |
| `Array[Symbol]`                        | `sym_array`                           |
| `Array[Foo]`                           | `obj_Foo_ptr_array`                   |
| `Array[<other>]`                       | `poly_array`                          |
| `Hash[String, Integer]`                | `str_int_hash`                        |
| `Hash[String, String]`                 | `str_str_hash`                        |
| `Hash[String, <other>]`                | `str_poly_hash`                       |
| `Hash[Symbol, Integer]`                | `sym_int_hash`                        |
| `Hash[Symbol, String]`                 | `sym_str_hash`                        |
| `Hash[Symbol, <other>]`                | `sym_poly_hash`                       |
| `T?`                                   | `<T>?`  (recursive)                   |
| `T \| nil` / `nil \| T`                | `<T>?`                                |

### Members emitted

- `def name: (...) -> R` -- instance method (`meth`)
- `def self.name: (...) -> R` -- class method (`cmeth`)
- `def self?.name: (...) -> R` -- emits both `meth` and `cmeth`
- `attr_accessor`, `attr_reader`, `attr_writer` -- emits `ivar`

### Unqualified type resolution

Inside `module Foo; class Bar`, an unqualified reference like
`def record: () -> Base` is resolved to `obj_Foo_Base` (single-level
parent fallback). A wrong guess is a no-op because the analyzer
silently drops seeds for unknown types -- no symbol table is required.
Covers the common sibling-in-module pattern; full lexical lookup is
not implemented.

## Dropped (silently skipped)

### Method signature shapes

- Multiple overloads -- only the first overload is considered; if the
  first is itself out-of-subset the whole method is skipped.
- Optional positional params (`?String`)
- Rest positional params (`*args`)
- Trailing positional params
- Required keyword params (`name: T`)
- Optional keyword params (`?name: T`)
- Rest keyword params (`**rest`)
- Block params (`{ ... }`, `?{ ... }`)
- Generic method params (`[X]`)
- Proc / untyped function types

A signature that touches any of the above is dropped wholesale rather
than emitted partially.

### Param-level -- any one of these in any required positional kills the whole signature

- A type that doesn't reduce to a supported tag.
- A generic container that isn't `Array[T]` or `Hash[K, V]`.
- A union that isn't `T | nil`.

### Type-level

- `self`, `instance`, `class`, `top`, `bot`, `untyped` / `any`
- Interfaces (`_Foo`)
- Intersections (`T & U`)
- Literal types (`:foo`, `42`, `"x"`)
- Type aliases
- Type variables (generics with parameters)
- Records (`{ name: T, ... }`)
- Tuples (`[T, U]`)
- Proc types (`^(T) -> U`)
- `Hash[K, V]` where K is not `String` or `Symbol`

### Members

- `include`, `extend`, `prepend` -- mixin ancestry not modeled
- `public`, `private`
- `alias`
- `@ivar` / `@@cvar` declarations (use `attr_*` for ivars)

## Seed file format

Read and applied by `apply_rbs_seeds` in `src/analyze.c` (it both parses
the seed file and pins the named params / returns / ivars before the
inference fixpoint):

```
class <QualifiedName>           # enter class scope; nested names use `_`
meth <name> <ret> <ptypes>      # `-` means "leave alone"; ptypes is
cmeth <name> <ret> <ptypes>     # comma-separated, or `-` for nullary
ivar <name> <type>
```

Lines whose first token isn't a keyword are treated as comments.

### How the analyzer reads a `<T>?` token

`parse_seed_type` pins a nilable token to the base type only where that
type's C slot still has an inhabitant left to spell nil with:

| Token                         | Pinned to        | nil is        |
|-------------------------------|------------------|---------------|
| `int?`                        | `mrb_int`        | `SP_INT_NIL`  |
| `float?`                      | `mrb_float`      | a NaN payload |
| `string?`, `obj_X?`, `*_array?`, `*_hash?` | the pointer | `NULL`   |
| `bool?`, `symbol?`            | `sp_RbVal`       | `SP_TAG_NIL`  |

`mrb_bool` and `sp_sym` have no spare inhabitant -- `0` is `false`, and
symbol `0` is a real symbol -- so `bool?` and `Symbol?` pin to the boxed
tagged union rather than collapsing nil onto `false` / `:""` (#3412).
A poly value narrowed into one of the sentinel-carrying slots goes
through `sp_poly_as_int_or_nil` / `sp_poly_as_float_or_nil`, which map
the nil tag to the sentinel instead of reading the zero payload beneath
it.

## Follow-up

Tracked as out-of-scope for the initial spike (see matz/spinel#7 +
OriPekelman/tep#6):

- Multi-overload resolution
- Generics with type variables
- Mixin ancestry from `include` / `extend`
- Return-type pinning where the body would otherwise cascade-widen
