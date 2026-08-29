# Random.urandom IS the OS entropy source in CRuby. spinel's was a PCG seeded
# from time()/clock() -- deterministic per run, from before the tree had a real
# source -- so two processes started in the same second drew the same bytes.
# It now goes through sp_crypto_entropy, the one place that decides what counts
# as secure, and fails closed (#4174).
a = Random.urandom(16)
b = Random.urandom(16)
p a.length
p a != b
p Random.urandom(0).length
p Random.urandom(1000).length          # longer than one draw's cap
p Random.urandom(1000).length == 1000
begin
  Random.urandom(-1)
rescue ArgumentError
  puts "ArgumentError"
end
# every byte value is reachable, and a NUL does not cut the draw short
seen = {}
40.times { Random.urandom(256).each_byte { |x| seen[x] = true } }
p seen.length > 200
p Random.urandom(4096).length
