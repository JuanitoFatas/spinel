# Deleting from the general hash -- the storage for a hash no typed kind
# fits: keys other than all Strings, all Symbols or all Integers, or Integer
# keys with values of more than one kind -- in place: the slot vacated, the
# neighbours shifted back over it and the insertion order kept, with every
# remaining key still found in order; a delete_if, select!, reject!, filter!,
# keep_if and shift; a default and a default proc; dup, merge, ==, to_a and
# invert after deletes; keys of every kind; hash hooks that allocate or
# store into the receiver, which the shift never runs, and one that raises,
# with the hash left as it was.

h = {"s" => :sym}
200.times { |i| h[i] = i.to_s }
p h.delete("s"), h.size, h.keys.first(3), h.keys.last(3)
0.step(199, 3) { |i| h.delete(i) }
p h.size
p h.keys.select { |k| !h.key?(k) || h[k] != k.to_s }
p h.keys.first(8), h.keys.last(4)
0.step(199, 3) { |i| h[i] = "r#{i}" }
p h.size, h.keys.last(5), h[3], h[198]
h[1000.5] = :f
p h.keys.last(2)
p h.delete(:nope), h.delete("s"), h.delete(:nope) { |k| "no #{k}" }, h.size

g = {1 => :a, 1.0 => :b, nil => :c, [1, 2] => :d, "1" => :e, :x => :f}
p g.delete(1.0), g.delete([1, 2]), g
g[[1, 2]] = :again
p g.keys, g[[1, 2]], g[nil]
p g.delete(nil), g.keys

w = {"k" => 1, 0 => 0}
50.times { |i| w[i] = i }
w.delete_if { |k, v| v.is_a?(Integer) && v % 5 == 0 }
p w.size, w.keys.first(6)
w.select! { |k, v| v.is_a?(Integer) && v.odd? }
p w.size, w.keys.first(6), w.keys.last(3)
w.reject! { |k, v| v > 40 }
p w
d = w.dup
d.delete(1)
d.delete(3)
p d.size, w.size, d == w, d.merge(w).size, w.to_a.first(2), w.invert.keys.first(2)
w.clear
3.times { |i| w[i] = i }
p w

q = {"a" => 1, 2 => "b", :c => 3.0, 4.5 => nil, nil => [1]}
p q.delete(q.keys[2]), q.keys
p q.delete(q.keys.last), q.keys
until q.empty?
  k = q.keys.first
  p [k, q.delete(k)]
end
p q, q.delete(1), q.delete(:a), q.shift, q

# shift takes the first entry; filter! and keep_if delete through the same
# path; a default and a default proc answer for the deleted keys after
sh = {:a => 1, "b" => 2, 3 => :c, 4.0 => nil}
p sh.shift, sh.shift, sh.keys, sh.shift, sh.shift, sh.shift, sh
f = {1 => "one", :two => 2, "three" => 3.0, [4] => 4}
p f.filter! { |k, v| k.is_a?(Integer) || k.is_a?(String) }, f.keep_if { |k, v| v != 3.0 }, f
p f.delete(1) { |k| :unused }, f.delete(1) { |k| "gone #{k}" }, f
dv = Hash.new(:none)
dv[1] = :one
dv["two"] = :two
p dv.delete(1), dv[1], dv["two"], dv.delete(:x), dv.size
dp = Hash.new { |hash, k| hash[k] = k.to_s * 2 }
dp[1] = "a"
dp[:b] = "b"
p dp.delete(1), dp[1], dp.delete(:b), dp[:b], dp.keys

# a key with a hash hook of its own, which allocates, over deep collisions:
# the hook runs once per probe, never in a delete's shift or a growth
class K
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; ("pad" * 20).size + @a % 7; end
  def eql?(o); o.is_a?(K) && @a == o.a; end
  def ==(o); eql?(o); end
  def to_s; "K#{@a}"; end
end
u = {"seed" => 0}
ks = (0...120).map { |i| K.new(i) }
ks.each { |k| u[k] = k.a * 10 }
ks.each_with_index { |k, i| u.delete(k) if i % 3 == 1 }
p u.size, ks.count { |k| u.key?(k) }, u.keys.map(&:to_s).first(5)
u.delete_if { |k, v| v % 20 == 0 }
p u.size, u.keys.map(&:to_s).first(5)
ks.each { |k| kk = K.new(k.a); u[kk] = k.a }
p u.size, u.keys.map(&:to_s).last(3), u[K.new(1)], u[K.new(7)], u[K.new(67)]

# a hash hook that stores into the receiver runs once, when its key arrives
# or is looked up; a delete's shift reads the hashes the table keeps
class R
  attr_reader :a
  @@h = nil
  def self.h=(x); @@h = x; end
  def initialize(a); @a = a; end
  def hash
    if @@h && @a >= 0 && @a % 10 == 0
      nk = R.new((0 - @a) - 1)
      @@h[nk] = :in unless @@h.key?(nk)
    end
    60 + @a % 5
  end
  def eql?(o); o.is_a?(R) && @a == o.a; end
  def ==(o); eql?(o); end
end
re = {"seed" => 0}
rs = (0...60).map { |i| R.new(i) }
rs.each { |r| re[r] = r.a }
R.h = re
rs.each_with_index { |r, i| re.delete(r) if i % 3 == 0 }
p re.size, rs.count { |r| re.key?(r) }, re.keys.count { |k| k.is_a?(R) && k.a < 0 }
R.h = nil
p re.keys.map { |k| k.is_a?(R) ? k.a : k }.sort_by(&:to_s).first(6)

# the hook runs once per probe a delete makes, never for the neighbours
$calls = 0
class Q
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; $calls += 1; 60 + @a % 5; end
  def eql?(o); o.is_a?(Q) && @a == o.a; end
end
qs = (0...120).map { |i| Q.new(i) }
c = {"s" => 0}
qs.each { |q| c[q] = q.a }
$calls = 0
c.delete(qs[7])
p $calls <= 3
$calls = 0
c.delete_if { |k, v| k.is_a?(Q) && k.a % 40 == 0 }
p $calls <= 3, c.size

# a hash hook that raises during a delete leaves the hash as it was
$boom = false
class E
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; raise "boom #{@a}" if $boom && @a == 6; 60 + @a % 3; end
  def eql?(o); o.is_a?(E) && @a == o.a; end
end
es = (0...12).map { |i| E.new(i) }
eh = {"s" => -1, 99 => -2}
es.each { |e| eh[e] = e.a }
$boom = true
begin
  eh.delete(es[6])
rescue => ex
  p ex.message
end
$boom = false
p eh.size, es.count { |e| eh[e] == e.a }, eh.keys.count { |k| !eh.key?(k) }
