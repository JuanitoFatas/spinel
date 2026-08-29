# The rendered forms: every method's length, alphabet and layout.
#
# A CSPRNG's output cannot be compared to a fixed expectation, so these are
# PROPERTIES -- the things that stay true across draws, which is also where
# the bugs live (a modulo that never emits a character, a uuid whose version
# nibble is not 4, a base64 that keeps its padding when it should not).
require "securerandom"

# hex: 2n lowercase hex characters
puts SecureRandom.hex(16).length
puts SecureRandom.hex(16).match?(/\A[0-9a-f]{32}\z/)
puts SecureRandom.hex(1).match?(/\A[0-9a-f]{2}\z/)
puts SecureRandom.hex.length          # the default n is 16

# uuid: RFC 9562 v4 -- version nibble 4, variant nibble 8..b
puts SecureRandom.uuid.length
puts SecureRandom.uuid.match?(/\A[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\z/)
puts SecureRandom.uuid_v4.match?(/\A[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\z/)

# alphanumeric: exactly n characters from A-Za-z0-9
puts SecureRandom.alphanumeric(24).length
puts SecureRandom.alphanumeric(24).match?(/\A[A-Za-z0-9]{24}\z/)
puts SecureRandom.alphanumeric(1).match?(/\A[A-Za-z0-9]\z/)
puts SecureRandom.alphanumeric.length # the default n is 16
puts SecureRandom.alphanumeric(0)     # empty, not a raise

# base64 pads, urlsafe_base64 does not (and uses -_ rather than +/)
puts SecureRandom.base64(12).length
puts SecureRandom.base64(12).match?(/\A[A-Za-z0-9+\/]{16}\z/)
puts SecureRandom.base64(10).match?(/\A[A-Za-z0-9+\/]{14}==\z/)
puts SecureRandom.urlsafe_base64(12).match?(/\A[A-Za-z0-9_-]{16}\z/)
puts SecureRandom.urlsafe_base64(10).match?(/\A[A-Za-z0-9_-]{14}\z/)
puts SecureRandom.urlsafe_base64(10, true).match?(/\A[A-Za-z0-9_-]{14}==\z/)

# Draws differ. Fifty uuids colliding would be a broken generator, not luck.
ids = []
50.times { ids << SecureRandom.uuid }
puts ids.uniq.length
toks = []
50.times { toks << SecureRandom.alphanumeric(16) }
puts toks.uniq.length

# Every alphanumeric character is reachable: over enough draws all 62 turn
# up. A `% 62` on a byte would still pass this -- the point it defends is
# the opposite mistake, an alphabet indexed short.
seen = {}
200.times { SecureRandom.alphanumeric(32).each_char { |ch| seen[ch] = true } }
puts seen.keys.length
