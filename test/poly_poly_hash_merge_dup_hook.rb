# A store, lookup or delete on the general hash -- the storage for a hash no
# typed kind fits: keys other than all Strings, all Symbols or all Integers,
# or Integer keys with values of more than one kind -- runs the key's hash
# hook, and a hook that allocates can collect: a key or value that arrived
# as a temporary was held by nothing the collector could see, and so was the
# table merge, dup and replace build or refill while the hook runs. The hook
# collects outright here, so it showed without SPINEL_GC_STRESS=1.
class K
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; GC.start; ("p" * 30).size + @a % 5; end
  def eql?(o); o.is_a?(K) && @a == o.a; end
  def ==(o); eql?(o); end
end
g = {"s" => 1, :t => 0}
(0...150).each { |i| kk = K.new(i); g[kk] = i }
h2 = {"x" => 9, :y => 0}
h2.replace(g)
p h2.size, h2.values.sum, h2.keys.count { |k| k.is_a?(K) }
d = g.dup
p d.size, d == g, d.values.sum
m = g.merge({"t" => 2, K.new(150) => 150})
p m.size, m.values.sum, g.size
p g.merge(h2).size, d.merge(m) { |k, a, b| a + b }.values.sum

# a key that arrives as a temporary, and a value that does with its key
# held, are rooted for the hook now
tk = {"seed" => 0.5}
100.times { |i| tk[K.new(i)] = i }
p tk.size, (0...100).count { |i| tk[K.new(i)] == i }
p tk.delete(K.new(3)), tk.size, tk.key?(K.new(3))
60.times { |i| tk.delete(K.new(i)) }
p tk.size, (0...100).count { |i| tk.key?(K.new(i)) }
class V
  attr_reader :n
  def initialize(n); @n = n; end
end
tv = {"seed" => 0.5}
100.times { |i| kk = K.new(i); tv[kk] = V.new(i) }
p tv.size, (0...100).count { |i| kk = K.new(i); tv[kk].is_a?(V) && tv[kk].n == i }
h3 = {[1] => :a, "s" => 1, 2 => :b}
h3.replace(h3)
p h3.size, h3.keys
