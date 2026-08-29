# Hash methods on a receiver whose class is only known at run time, and the
# names Array and Hash share. Covers merge!, compact! and the in-place
# filters writing back into a typed hash of each layout (Symbol keys,
# String keys with Integer values, String values, and the general hash);
# assoc, rassoc, fetch_values, compact! and the filters on a parameter that
# is an Array at one call site and a Hash at another; the value being the
# receiver, so a write through it and a second merge! argument reach the
# original; an argument that is a Hash at run time only; a nil a
# String-valued hash already held; the result flowing into a slot the
# inference widened to poly; the frozen original a mutator refuses before
# it runs; and the NoMethodError a receiver of neither kind raises.
cells = [{a: 1, b: nil}, 0]
h = cells[0]
p h.merge!({c: 3})
p h.compact!
p cells[0]

counts = [{"x" => 1}, "s"]
c = counts[0]
p c.merge!({"y" => 2})
p counts[0]

names = [{"k" => "v"}, 1.5]
n = names[0]
p n.merge!({"k2" => "v2"})
p names[0]

mixed = [{1 => "one", "two" => 2}, :s]
m = mixed[0]
p m.merge!({3.0 => nil})
p m.compact!
p mixed[0]

def pair_for(x, k)
  x.assoc(k)
end
def pair_with(x, v)
  x.rassoc(v)
end
def pick(x, k1, k2)
  x.fetch_values(k1, k2)
end
def drop_nils(x)
  x.compact!
end

p pair_for([[1, :a], [2, :b]], 2)
p pair_for({1 => :a, 2 => :b}, 2)
p pair_with([[1, :a], [2, :b]], :a)
p pair_with({1 => :a, 2 => :b}, :a)
p pick([10, 20, 30], 0, 2)
p pick({"p" => 10, "q" => 20}, "q", "p")
p drop_nils([1, nil, 2])
p drop_nils({a: 1, b: nil})
p drop_nils([1, 2])
p drop_nils({a: 1})

# the value is the receiver, as CRuby answers it: a write through it, and
# the second argument of a merge! (which folds into a chain of one-argument
# calls), reach the original; update is merge!'s other name
d = [{a: 1}, 0]
dh = d[0]
p dh.merge!({b: 2}, {c: 3})
p d[0]
dh.merge!({d: 4})[:z] = 9
p d[0]
p dh.merge!({}).equal?(dh)
p dh.update({y: 0})
p dh.merge!({b: nil}).compact!
p d[0]

# an argument that is a Hash at run time only is checked there
def fold(x, y)
  x.merge!(y)
end
p fold({a: 1}, {b: 2})
p fold({"s" => 1}, {"t" => 2})
begin
  fold({a: 1}, [1])
rescue TypeError => e
  puts e.message
end

# a String-valued hash keeps a nil it already held
ns = {"a" => "x"}
ns["b"] = "x"[5]
p [ns, 0][0].merge!({"c" => "y"})
p ns

# a slot that held a typed value on an early pass and the box on the last
v = {z: 0}
v = [{y: 1, w: nil}, 5][0]
p v.compact!

begin
  pick("str", 0, 1)
rescue NoMethodError => e
  puts e.message
end
begin
  drop_nils(7)
rescue NoMethodError => e
  puts e.message
end

# a frozen original is refused before the mutator runs, and stays as it was
f = [{a: 1, b: nil}.freeze, 0]
begin
  f[0].merge!({c: 3})
rescue FrozenError => e
  p e.class
end
begin
  f[0].compact!
rescue FrozenError => e
  p e.class
end
p f[0]

# a String the program appends to after storing it elsewhere is held as a
# shared handle; merged in as a key or a value it goes into the original by
# its contents, like any other String
sh = "x".dup
keep = [sh]
sh << "y"
m = [{"k" => "v"}, 0][0]
m.merge!({"b" => sh, sh => "w"})
p m
n = [{1 => "v"}, 0][0]
n.update({2 => sh})
p n
p keep

# the in-place filters write back into a typed hash of each layout and into
# the general hash; the value is the receiver, or nil when a ! form removed
# nothing, so a write through it reaches the original
fa = [{a: 1, b: 2, c: nil}, 0]
p fa[0].select! { |k, v| v }
p fa[0].reject! { |k, v| false }
p fa[0].keep_if { |k, v| k == :a }
p fa[0]
fb = [{"x" => 1, "y" => 2}, "s"]
p fb[0].delete_if { |k, v| v > 1 }
p fb[0]
fc = [{"k" => "v", "k2" => "w"}, 1.5]
p fc[0].filter! { |k, v| v == "w" }
p fc[0]
fd = [{1 => "one", "two" => 2, :three => 3.0}, :s]
p fd[0].reject! { |k, v| k.is_a?(String) }
p fd[0].select! { |k| k == 1 }
p fd[0]
fe = [{a: 1, b: 2}, 0]
fe[0].delete_if { |k, v| v == 2 }[:z] = 9
p fe[0]
p fe[0].keep_if { |k, v| true }.equal?(fe[0])

def keep(x)
  x.select! { |e| e.to_s.size > 1 }
end
def drop(x)
  x.delete_if { |e| e == 1 }
end
p keep([10, 2, 300])
p keep({a: 1, bb: 2})
p drop([1, 2, 3])
p drop({1 => :a, 2 => :b})

# a slot that held a typed value on an early pass and the box on the last
w = {z: 0}
w = [{y: 1, x: nil}, 5][0]
p w.reject! { |k, v| v.nil? }

f = [{a: 1, b: nil}.freeze, 0]
begin
  f[0].select! { |k, v| puts "ran"; v }
rescue FrozenError => e
  p e.class
end
p f[0]
begin
  drop(7)
rescue NoMethodError => e
  puts e.message
end
