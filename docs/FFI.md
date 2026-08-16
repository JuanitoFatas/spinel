# Spinel FFI

Call C functions from Spinel Ruby programs. No extension compiler, no
`require "ffi"`: declarations go straight into the source and the AOT
compiler generates direct C call sites with the right externs and
linker flags.

## Example

```ruby
module LibC
  ffi_func :strlen, [:str], :size_t
  ffi_func :getpid, [],     :int
end

puts LibC.strlen("hello, world")   # 12
puts LibC.getpid
```

Compile and run:

```sh
./spinel prog.rb && ./prog
```

`libc` and `libm` are always linked; anything else needs `ffi_lib`.

## DSL reference

All FFI declarations go inside a `module` body. The module name becomes
the namespace for the functions (`RAY.InitWindow`, `LibC.strlen`, …).

### `ffi_lib "name"`

Declares that this module needs `-lname` on the link command line. May
appear multiple times per module.

```ruby
module SQL
  ffi_lib "sqlite3"
end
```

### `ffi_cflags "..."`

Declares cflags (include dirs, defines, link-search paths) needed for
this module's externs. Rarely needed — externs use standard C types
only, so headers don't have to be included in the generated code — but
useful when a library is installed somewhere non-standard.

```ruby
ffi_cflags "-I/usr/local/include"
ffi_cflags "-Wl,-rpath,/usr/local/lib"
```

### `ffi_source "..."`

Embeds a compile-time C source fragment directly into Spinel's generated
translation unit. This is intended for small adapters that should travel in a
single Ruby source file; normal package C should still use carried `.c` files so
it can be compiled and cached independently.

```ruby
module Tiny
  ffi_source <<~C
    #include <stdint.h>
    intptr_t tiny_double(intptr_t n) { return n * 2; }
  C
  ffi_func :tiny_double, [:long], :long
end
```

The argument must be a compile-time string (a literal/heredoc, adjacent
literals, `String#+`, `__dir__`, or `File.expand_path` over those forms). The
fragment is emitted after `spinel_rt.h` and FFI declarations but before the
generated program's function bodies. Use `ffi_cflags` and `ffi_lib` for any
headers and libraries it needs. Because this is native C, it has the same trust
and portability implications as a package-carried `.c` file.

### `ffi_func :name, [arg_types], ret_type`

Declares a C function callable as `Module.name(...)`.

```ruby
ffi_func :sqlite3_open,        [:str, :ptr],                   :int
ffi_func :sqlite3_close,       [:ptr],                         :int
ffi_func :sqlite3_exec,        [:ptr, :str, :ptr, :ptr, :ptr], :int
ffi_func :sqlite3_errmsg,      [:ptr],                         :str
```

Recognized type specs:

| spec | C type | Spinel type |
|---|---|---|
| `:int` | `int` | `int` |
| `:uint32` | `uint32_t` | `int` |
| `:int32` | `int32_t` | `int` |
| `:uint16` | `uint16_t` | `int` |
| `:int16` | `int16_t` | `int` |
| `:uint8` | `uint8_t` | `int` |
| `:int8` | `int8_t` | `int` |
| `:size_t` | `size_t` | `int` |
| `:long` | `long` | `int` |
| `:float` | `float` | `float` |
| `:double` | `double` | `float` |
| `:bool` | `int` | `bool` |
| `:str` | `const char *` | `string` (NUL-terminated) |
| `:binstr` | `const char *` | `string` (binary-safe, return only) |
| `:ptr` | `void *` | `ptr` |
| `:float_array` | `const double *` | `Array<Float>` (`.data` pointer) |
| `:int_array` | `const int64_t *` | `Array<Int>` (`.data` pointer) |
| `:void` | `void` | `void` (return only) |

All integer types collapse to `mrb_int` (int64) inside Spinel and are
cast to the declared C type at the call boundary. Floats collapse to
`double` the same way.

`:str` builds the result String by `strlen`, so it stops at the first
embedded NUL. `:binstr` is a return-only variant that builds a
binary-safe String of an exact byte count instead (it reads
`sp_ffi_bin_len`, which the callee sets to the exact byte count
just before returning), so embedded NUL bytes are preserved. Use it for
binary socket reads or raw digests where `:str` would truncate.

`:float_array` / `:int_array` hand the C side a pointer to the Spinel
Array's contiguous storage (`.data`). Length is **not** part of the
spec — pass it as a separate `:size_t` arg, same way as `:str` +
`strlen`. Lifetime is call-duration only: the GC may free the
underlying Array after the call returns, so the C side must not
stash the pointer (copy if it needs to).

### `ffi_const :NAME, <int>`

Declares an integer constant accessible as `Module::NAME`. Pure
convenience — the value is inlined at use sites like any other Ruby
integer constant.

```ruby
ffi_const :SQLITE_OK,   0
ffi_const :SQLITE_ROW,  100
ffi_const :SQLITE_DONE, 101
```

### `ffi_buffer :name, <size>`

Declares a static `size`-byte buffer, accessible as `Module.name`
returning a `:ptr`. Useful as scratch space or as an out-parameter for
functions like sqlite3's `sqlite3_open`, which writes the database
handle into a caller-supplied `sqlite3 **`.

```ruby
ffi_buffer :db_out, 8
SQL.sqlite3_open(":memory:", SQL.db_out)
db = SQL.read_ptr(SQL.db_out)  # the actual sqlite3 *
```

Lifetime: static. The buffer lives for the whole program.

### `ffi_read_<width> :name, <offset>` / `ffi_read_ptr`

Declares a field reader: `Module.name(buf)` returns the value at
`offset` bytes into `buf`. Handy for poking into C structs when you
only need a few fields, or for reading back what a C function wrote
into a buffer you handed it.

The width suffix is one of `u8`, `u16`, `u32`, `u64`, `i8`, `i16`,
`i32`, `i64`, and it is the width of the load: `ffi_read_u8` reads one
byte, not four. A signed suffix sign-extends. `ffi_read_ptr` reads a
`void *`. Any other suffix is refused at the call site rather than
guessed at.

`ffi_write_<width> :name, <offset>` / `ffi_write_ptr` is the mirror
image: `Module.name(buf, val)` stores `val` at `offset` and returns it,
with the same suffixes and the same widths.

Applied to a declared `ffi_buffer`, the access is bounds-checked while
compiling: `offset + width` past the buffer's size is refused at the call
site, since both terms are known there. A pointer from anywhere else -- a
C return value, a callback parameter, a local holding a `:ptr` -- carries
no size, so it stays unchecked and is yours to get right.

```ruby
# sqlite3_open(path, ppDb) writes the new db handle into *ppDb.
# Pull the pointer out of our scratch buffer at offset 0.
ffi_read_ptr :read_ptr, 0

db = SQL.read_ptr(SQL.db_out)
```

### `ffi_struct :Name, [[:field, :spec], ...]`

Declares a named C struct and generates its accessors: `Module.Name_new`
allocates one and returns a boxed pointer, `Module.Name_get_<field>(ptr)`
reads a field and `Module.Name_set_<field>(ptr, val)` writes one. The C
compiler owns the layout, so the accessors are plain member access and the
offsets are whatever the target ABI says -- unlike `ffi_buffer` +
`ffi_read_*`, where you supply the offsets yourself.

```ruby
module M
  ffi_struct :Point, [[:x, :long], [:y, :long]]
end

pt = M.Point_new
M.Point_set_x(pt, 3)
puts M.Point_get_x(pt)     # => 3
```

A struct pointer is a `:ptr`, so it can be handed to any `ffi_func`
argument declared that way -- which is how a C function fills one in.

### `ffi_callback :name, [arg_types], ret_type`

Declares a C function-pointer type usable as an `ffi_func` argument spec.
Passing `method(:some_method)` to an argument of that type compiles a
trampoline that converts the C arguments, calls the method, and converts the
result back, so a Ruby method can be handed to a C API that takes a callback.

```ruby
module L
  ffi_callback :cmp,   [:ptr, :ptr], :int
  ffi_func     :qsort, [:int_array, :size_t, :size_t, :cmp], :void
  ffi_read_i32 :val,   0
end

def cmp(a, b) = L.val(a) <=> L.val(b)

L.qsort(nums, nums.size, 8, method(:cmp))
```

A function that takes a callback has its `extern` skipped and the header
prototype called directly: the per-argument `const` qualification of such a
prototype (`qsort` takes `void *base`, `bsearch` a `const void *`) is not
something the declaration can reproduce.

## Pointer semantics

`:ptr` maps to C `void *`. Values of this type are **not GC-tracked**:
the Spinel garbage collector never follows them and never frees them.
Foreign memory is the user's responsibility.

Two consequences worth knowing:

1. **Call destroy functions explicitly.** Nothing calls `sqlite3_close`,
   `sqlite3_finalize`, or `free()` for you.
2. **Strings passed into C are only valid for the duration of the
   call.** Spinel strings are GC-managed; if a C function stashes the
   pointer somewhere and the string becomes unreachable afterward, a
   later GC cycle will free it out from under the C code. If you need
   a string to outlive the call, copy it into an `ffi_buffer` first.

`ptr` values compare equal to `nil` when the pointer is NULL:

```ruby
db = SQL.read_ptr(SQL.db_out)
if db == nil
  puts "could not open database"
end
```

## Link-flag plumbing

The codegen emits marker comments into the generated C:

```c
/* SPINEL_LINK: -lsqlite3 */
/* SPINEL_CFLAGS: -I/usr/local/include */
```

The `spinel` compiler scrapes these from the generated C in-process and
appends them to the `cc` invocation. If you want to override (e.g. static
linking or a custom lib path), use `-c` to stop at C and drive the linker
yourself.

## Limitations

Scalars, strings, opaque pointers, integer constants, raw byte buffers,
struct declarations and callbacks are covered. Not supported yet:

- **No variadic C functions** (`printf(...)`). Use Spinel's built-in
  `printf` if you want formatted output.
- **Pointers can't enter polymorphic values.** Don't put a `:ptr` into
  a `poly_array` or a generic `Hash`; keep them as plain locals or
  wrap them in a class with a `ptr`-typed ivar.

## Examples

Runnable examples live under `examples/ffi/`:

- `examples/ffi/libm/`     — libc / libm smoke (cos, sqrt, pow, strlen, getpid)
- `examples/ffi/sqlite/`   — blog system (posts, tags, comments) on sqlite3

Each subdirectory has a `README.md` with build instructions and the
required system packages.
