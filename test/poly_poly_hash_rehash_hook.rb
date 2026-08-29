# The general hash -- the storage for a hash no typed kind fits: keys other
# than all Strings, all Symbols or all Integers, or Integer keys with values
# of more than one kind -- keeps each key's hash beside it, so a growing
# table runs no key's hash hook: one that allocated could collect from the
# middle of the layout, when the entries not yet moved were held by nothing
# the collector could see. The hook collects outright here, so the loss
# showed without SPINEL_GC_STRESS=1; each key is held by a local only while
# it is stored, and by the hash alone after. The hook runs once per store
# or lookup; Hash#rehash asks every key again, for a key changed since it
# was stored, and folds keys that have become eql? into the first with the
# last value.
class J
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; GC.start; ("pad" * 20).size + @a; end
  def eql?(o); o.is_a?(J) && @a == o.a; end
  def ==(o); eql?(o); end
end
r = {"seed" => 0.5}
200.times { |i| kk = J.new(i); r[kk] = i }
r.rehash
p r.size, (0...200).count { |i| kk = J.new(i); r[kk] != i }
p r.keys.count { |k| k.is_a?(J) }, r.keys.map { |k| k.is_a?(J) ? k.a : k }.uniq.size

a = [1]
h = {a => :arr, "s" => 1}
10.times { |i| h[100 + i] = i }
a << 2
p h[[1, 2]], h.key?(a), h[[1]]
40.times { |i| h[i] = i }
p h[[1, 2]], h.key?(a), h.size
p h.rehash.equal?(h), h[[1, 2]], h.key?(a), h[[1]], h.size
h.delete(a)
p h.size, h[[1, 2]]
f = {[3] => :x, 4 => :y}
f.freeze
begin
  f.rehash
rescue FrozenError => e
  p e.class
end

$calls = 0
class Q
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; $calls += 1; 60 + @a % 5; end
  def eql?(o); o.is_a?(Q) && @a == o.a; end
end
qs = (0...130).map { |i| Q.new(i) }
c = {"s" => 0}
qs.first(10).each { |q| c[q] = q.a }
$calls = 0
qs.drop(10).each { |q| c[q] = q.a }
p $calls
$calls = 0
c[qs[7]]
p $calls

# keys that have become eql? fold into the first, with the last value
x = [1]
y = [2]
z = [3]
d = {"s" => 0, x => :x, "t" => 9, y => :y, z => :z, "u" => 8}
y[0] = 1
z[0] = 1
d.rehash
p d.size, d.to_a, d[[1]]
d.delete([1])
p d.size, d[[1]]
