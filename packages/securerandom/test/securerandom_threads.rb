# Concurrent draws must not collide.
#
# The C side returns a pointer into a static buffer and the `:cbinstr` call
# site copies out of it AFTER the call returns. If that buffer were shared
# across workers, a second thread drawing inside that window would overwrite
# the first one's bytes -- and two sessions would be handed the same token.
# Nothing about that failure looks like a failure: the tokens are still the
# right length, the right alphabet, and random-looking.
#
# So: draw hard from several threads at once and count distinct results.
require "securerandom"

THREADS = 8
DRAWS   = 200

results = []
lock = Mutex.new
threads = []
THREADS.times do
  threads << Thread.new do
    mine = []
    DRAWS.times { mine << SecureRandom.hex(16) }
    lock.synchronize { results.concat(mine) }
  end
end
threads.each(&:join)

puts results.length
puts results.uniq.length
puts results.all? { |t| t.length == 32 }

# The same shape over the token spelling campfire actually mints.
toks = []
tlock = Mutex.new
ts = []
THREADS.times do
  ts << Thread.new do
    mine = []
    DRAWS.times { mine << SecureRandom.alphanumeric(24) }
    tlock.synchronize { toks.concat(mine) }
  end
end
ts.each(&:join)

puts toks.length
puts toks.uniq.length
puts toks.all? { |t| t.match?(/\A[A-Za-z0-9]{24}\z/) }
