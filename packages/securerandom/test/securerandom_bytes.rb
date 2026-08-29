# random_bytes is the primitive the rest render, so this is where the binary
# handling and the draw boundary get checked.
require "securerandom"

# Exact byte counts, including the boundaries of the C-side single draw
# (SPC_RANDOM_MAX is 256) and the chunked path above it.
[0, 1, 15, 16, 255, 256, 257, 512, 1000].each do |n|
  puts SecureRandom.random_bytes(n).bytesize
end
puts SecureRandom.random_bytes.bytesize   # the default n is 16
puts SecureRandom.bytes(24).bytesize      # CRuby's other name for it

# BINARY, not text. A draw carries NUL bytes about one time in 256, so a
# 4 KiB sample essentially always contains one; a result that stopped at the
# first NUL would be short, and one counted as UTF-8 code points would be
# too. Both would pass a test that only checked "looks random".
sample = SecureRandom.random_bytes(4096)
puts sample.bytesize
puts sample.include?("\x00")

# Every byte value is reachable -- a draw masked or clamped somewhere would
# show up as a missing high byte.
seen = {}
SecureRandom.random_bytes(4096).each_byte { |b| seen[b] = true }
puts seen.keys.length > 200

# Successive draws differ.
puts SecureRandom.random_bytes(32) == SecureRandom.random_bytes(32)

# hex over a known length is exactly the bytes, twice over.
puts SecureRandom.hex(300).length

# A negative size is an ArgumentError, as in CRuby.
begin
  SecureRandom.random_bytes(-1)
  puts "no raise"
rescue ArgumentError
  puts "ArgumentError"
end
begin
  SecureRandom.alphanumeric(-1)
  puts "no raise"
rescue ArgumentError
  puts "ArgumentError"
end
