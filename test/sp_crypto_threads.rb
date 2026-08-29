# The return buffers in lib/sp_crypto.c must be per-thread, not per-process.
#
# Every entry point in that file answers with a pointer into a file-static
# buffer, and the call site copies out of it AFTER the call has returned: a
# `:cstring` native_func dups onto the GC heap, an ffi `:str` is read by
# whatever reads it next. A buffer shared across workers gives a second thread
# a window to enter the same function and overwrite the first one's answer
# before the first has taken its copy.
#
# Nothing about that failure looks like a failure. A clobbered digest is still
# 64 hex characters and a clobbered HMAC is still a signature; it fails a
# verification somewhere else, later, with nothing pointing back here. So the
# check is an oracle rather than a shape: compute the answers with one thread
# running, where no clobber is possible, then recompute them under contention
# and count the disagreements.
require "digest"

module Crypto
  ffi_func :sp_crypto_hmac_sha256_hex, [:str, :str], :str
end

# An anchor, so a run in which every digest is uniformly wrong cannot agree
# with its own oracle and pass: SHA-256("abc"), the FIPS 180-4 vector.
puts Digest::SHA256.hexdigest("abc")

KEY = "signing-key"
inputs = []
64.times { |i| inputs << "message-#{i}" }

sha256 = {}
sha1   = {}
hmac   = {}
inputs.each do |m|
  sha256[m] = Digest::SHA256.hexdigest(m)
  sha1[m]   = Digest::SHA1.hexdigest(m)
  # `+ ""` copies out of the ffi buffer here for the same reason the digest
  # packages dup: the pointer is only good until this thread calls again.
  hmac[m]   = Crypto.sp_crypto_hmac_sha256_hex(KEY, m) + ""
end

THREADS = 8
ROUNDS  = 25

wrong = 0
lock = Mutex.new
threads = []
THREADS.times do
  threads << Thread.new do
    mine = 0
    ROUNDS.times do
      inputs.each do |m|
        mine += 1 if Digest::SHA256.hexdigest(m) != sha256[m]
        mine += 1 if Digest::SHA1.hexdigest(m) != sha1[m]
        mine += 1 if (Crypto.sp_crypto_hmac_sha256_hex(KEY, m) + "") != hmac[m]
      end
    end
    lock.synchronize { wrong += mine }
  end
end
threads.each(&:join)

puts wrong
