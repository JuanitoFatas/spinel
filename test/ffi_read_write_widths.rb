# Every `ffi_read_*` / `ffi_write_*` suffix names the width of the access.
# The bytes below are laid out by a wider write and read back one width at a
# time, so a reader that loaded the wrong width would fold in its neighbours.
# Little-endian target (x86_64 / arm64), which is what the FFI layer assumes.
module W
  ffi_buffer    :buf, 32

  ffi_write_u32 :w32, 0
  ffi_read_u8   :r8_0, 0
  ffi_read_u8   :r8_1, 1
  ffi_read_u8   :r8_2, 2
  ffi_read_u8   :r8_3, 3
  ffi_read_u16  :r16_0, 0
  ffi_read_u16  :r16_2, 2
  ffi_read_u32  :r32_0, 0

  ffi_write_i8  :wi8, 8
  ffi_read_i8   :ri8, 8
  ffi_read_u8   :ru8, 8

  ffi_write_i16 :wi16, 10
  ffi_read_i16  :ri16, 10
  ffi_read_u16  :ru16, 10

  ffi_write_i64 :wi64, 16
  ffi_read_i64  :ri64, 16

  ffi_write_u16 :w16_24, 24
  ffi_read_u32  :r32_24, 24

  # the last byte of the buffer: a wider load here would run off the end
  ffi_read_u8   :r8_last, 31
end

W.w32(W.buf, 0x04030201)
puts W.r8_0(W.buf)
puts W.r8_1(W.buf)
puts W.r8_2(W.buf)
puts W.r8_3(W.buf)
puts W.r16_0(W.buf)
puts W.r16_2(W.buf)
puts W.r32_0(W.buf)

W.wi8(W.buf, -5)
puts W.ri8(W.buf)
puts W.ru8(W.buf)

W.wi16(W.buf, -300)
puts W.ri16(W.buf)
puts W.ru16(W.buf)

W.wi64(W.buf, -1234567890123)
puts W.ri64(W.buf)

# a 2-byte store leaves the two bytes above it alone
W.w16_24(W.buf, 0xBEEF)
puts W.r32_24(W.buf)

puts W.r8_last(W.buf)
