# A hash store whose key and value are both built by the statement: the key
# is held by nothing while the value is built, so a collection the value's
# build runs swept it before the table stored it. Covers []= and store with a
# built key and a value built by a method that collects, on each hash kind
# (String keys with Integer and String values, Integer keys with Integer and
# String values, String, Symbol and general keys with poly values); a key and
# a value that are both new objects on the general hash, whose hash hook does
# not allocate, so the store alone is on trial; ||=, &&= and += with a built
# key; a store, a ||= and a &&= as values; a value that allocates without a
# call, a Range; a receiver that is a call, evaluated once, and one that is a
# temporary; the receiver read before the key and the value, and the key
# before the value; a frozen receiver refused after its key and value are
# evaluated, as CRuby refuses it; and a literal's pairs, which are stores
# too. Each value builder collects the object heap outright and allocates
# past the string heap's own trigger, so both are swept while the key is
# held by the statement alone. The Integer- and Symbol-keyed sections and
# the Integer-valued ones have nothing to sweep, the two order sections
# guard an order this compiler's argument evaluation happens to get right,
# and the Range section fails only under GC stress; they hold the emitter to
# the shape.
def gcv(i)
  GC.start
  ("pad" * 400_000).size
  "v#{i}"
end
def gci(i)
  GC.start
  ("pad" * 400_000).size
  i * 2
end

ss = {"a" => "b"}
100.times { |i| ss["k#{i}"] = gcv(i) }
p ss.size, ss.keys.uniq.size, ss["k50"]
100.times { |i| ss.store("s#{i}", gcv(i)) }
p ss.size, ss.keys.uniq.size, ss["s50"]

si = {"a" => 1}
100.times { |i| si["k#{i}"] = gci(i) }
p si.size, si.keys.uniq.size, si["k50"]

is = {1 => "a"}
100.times { |i| is[gci(i) + 1000] = gcv(i) }
p is.size, is[1100]

ii = {1 => 2}
100.times { |i| ii[gci(i) + 1000] = gci(i) }
p ii.size, ii[1100]

sp = {"a" => 1, "b" => "x"}
100.times { |i| sp["k#{i}"] = gcv(i) }
100.times { |i| sp["n#{i}"] = gci(i) }
p sp.size, sp.keys.uniq.size, sp["k50"], sp["n50"]

yp = {a: 1, b: "x"}
syms = %i[k0 k1 k2 k3 k4 k5 k6]
100.times { |i| yp[syms[i % 7]] = gcv(i) }
p yp.size, yp.keys.uniq.size, yp[:k1]

gp = {"a" => 1, :b => 2}
100.times { |i| gp["k#{i}"] = gcv(i) }
100.times { |i| gp[[i]] = gcv(i) }
p gp.size, gp.keys.uniq.size, gp["k50"], gp[[50]]

class K
  attr_reader :a
  def initialize(a); @a = a; end
  def hash; @a.hash; end
  def eql?(o); o.is_a?(K) && o.a == @a; end
end
class V
  attr_reader :b
  def initialize(b); GC.start; ("pad" * 400_000).size; @b = b; end
end
kv = {"a" => 1, :b => 2}
100.times { |i| kv[K.new(i)] = V.new(i) }
p kv.size
found = 0
100.times { |i| found += 1 if kv[K.new(i)].is_a?(V) && kv[K.new(i)].b == i }
p found

# ||= and += with a built key
oe = {"a" => "b"}
100.times { |i| oe["k#{i}"] ||= gcv(i) }
100.times { |i| oe["k#{i}"] ||= "again" }
p oe.size, oe.keys.uniq.size, oe["k50"]
pe = Hash.new("")
100.times { |i| pe["k#{i}"] += gcv(i) }
100.times { |i| pe["k#{i}"] += gcv(i) }
p pe.size, pe.keys.uniq.size, pe["k50"]
ie = Hash.new(0)
100.times { |i| ie["k#{i}"] += gci(i) }
p ie.size, ie.keys.uniq.size, ie["k50"]
ae = {"a" => "b"}
100.times { |i| ae["k#{i}"] = "seed" }
100.times { |i| ae["k#{i}"] &&= gcv(i) }
100.times { |i| ae["x#{i}"] &&= gcv(i) }
p ae.size, ae.keys.uniq.size, ae["k50"], ae["x50"]

# a receiver that is a call is evaluated once
$n = 0
$h = {"a" => "b"}
def mk; $n += 1; $h; end
100.times { |i| mk["k#{i}"] = gcv(i) }
p $n, $h.size, $h.keys.uniq.size
mk["x"] = "y"
mk["z"] ||= "w"
p $n
def fresh; {"a" => "b"}; end
100.times { |i| fresh["k#{i}"] = gcv(i) }
puts "fresh ok"

# the key is evaluated before the value
def kk(i); puts "key #{i}"; "k#{i}"; end
def vv(i); puts "value #{i}"; "v#{i}"; end
od = {"a" => "b"}
od[kk(1)] = vv(1)
od.store(kk(2), vv(2))
p od

# a frozen receiver is refused after its key and value are evaluated
fz = {"a" => 1}.freeze
begin
  fz[(puts "key"; "k")] = (puts "value"; 1)
rescue FrozenError => e
  p e.class
end
begin
  fz.store(kk(3), 3)
rescue FrozenError => e
  p e.class
end
p fz

# a store, a ||= and a &&= as values
vs = {"a" => "b"}
r = nil
100.times { |i| r = (vs["k#{i}"] = gcv(i)) }
p vs.size, vs.keys.uniq.size, vs["k50"], r
100.times { |i| r = (vs["o#{i}"] ||= gcv(i)) }
p vs.size, vs.keys.uniq.size, vs["o50"], r
100.times { |i| r = (vs["k#{i}"] &&= gcv(i)) }
p vs.size, vs.keys.uniq.size, vs["k50"], r
p((mk[kk(4)] = vv(4)))
p $n

# a value that allocates without a call
rv = {"a" => 1, "b" => "x"}
100.times { |i| rv["r#{i}"] = (i..i + 1) }
p rv.size, rv.keys.uniq.size, rv["r50"]

# the receiver is read before the key and the value
ro = {"a" => "b"}
orig = ro
ro[kk(5)] = (ro = {"z" => "y"}; "v")
p orig, ro

# a literal's pairs are stores too
bad = 0
100.times do |i|
  lt = {"a" => "b", "k#{i}" => gcv(i), "m#{i}" => gcv(i)}
  bad += 1 unless lt.size == 3 && lt["k#{i}"] == "v#{i}" && lt["m#{i}"] == "v#{i}"
  lg = {"a" => 1, :b => 2, "k#{i}" => gcv(i)}
  bad += 1 unless lg.size == 3 && lg["k#{i}"] == "v#{i}"
end
p bad
