# An int-keyed `x[i] = v` on a slot whose own type has not arrived yet is not
# evidence of a Hash: `xs = src.map { }` is an Array once the block's return
# settles, and pinning xs to an int-keyed hash in between poisons every method
# it reaches -- parameters only widen, so the later unify(hash, array) is poly
# for good. Every parameter and return below must stay sp_int.
module M
  def self.sub(a, b) = (a - b) % 97
  def self.mul(a, b) = (a * b) % 97
end

def consume(xs)
  s = 1
  i = 0
  while i < xs.length
    s = M.mul(s, xs[i])
    i += 1
  end
  s
end

src = [5, 7, 9]
shifted = src.map { |e| M.sub(e, 1) }
i = 0
while i < 3
  shifted[i] = 1 if shifted[i] == 0
  i += 1
end
puts consume(shifted)
