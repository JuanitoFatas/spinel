# Accessors that fit their declared ffi_buffer keep compiling; the ones that run
# past the end are refused at compile time (#3970, see the .err test).
module M
  ffi_buffer :a, 8
  ffi_read_u32  :first,  0
  ffi_read_u32  :second, 4
  ffi_write_u32 :put,    4
  ffi_read_u8   :byte7,  7
  ffi_write_u8  :put7,   7
  ffi_read_i16  :half6,  6
end
M.put(M.a, 0xDEADBEEF)
p M.second(M.a)
p M.first(M.a)
p M.byte7(M.a)
M.put7(M.a, 1)
p M.byte7(M.a)

# a pointer from elsewhere has no size to check against
module N
  ffi_func :malloc, [:size_t], :ptr
  ffi_func :free, [:ptr], :void
  ffi_write_u32 :put0, 0
  ffi_read_u32  :at0,  0
end
ptr = N.malloc(64)
N.put0(ptr, 7)
p N.at0(ptr)
N.free(ptr)
