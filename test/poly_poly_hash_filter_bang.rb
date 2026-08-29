# Hash#select! / #filter! / #reject! / #keep_if / #delete_if on the general
# hash -- the hash the inference could not fit to one of the typed layouts
# -- mutate the receiver in place, in expression position and as a bare
# statement. The `!` forms answer nil when nothing was removed and self
# otherwise; keep_if and delete_if always answer self. Keys of every kind
# reach the block, a one-parameter block takes the key alone, and the
# predicate is read by Ruby truthiness: only nil and false reject a pair,
# whether the predicate's value is poly, a nilable Integer, or the value a
# `next` left. A key added during the iteration raises, a pair deleted
# during it is skipped and no other.
def gh(x); x; end

h = gh({1 => "a", "b" => 2, :c => 3.5, nil => [4], 4.5 => "d"})
p h.select! { |k, v| k.is_a?(Integer) || v.is_a?(Integer) }
p h
p h.reject! { |k, v| v == 2 }
p h
p h.reject! { |k, v| false }
p h.select! { |k, v| true }
p h.keep_if { |k, v| true }
p h.delete_if { |k, v| k == 1 }
p h

# as a statement, and the order of what stays
g = gh({1 => "a", "b" => 2, :c => 3, 4.5 => "d"})
g.delete_if { |k, v| v == 2 }
p g
g.select! { |k, v| k != 4.5 }
p g
g.filter! { |k, v| k == :c }
p g
g.keep_if { |k, v| false }
p g
p g.empty?

# a one-parameter block sees the key
k1 = gh({1 => "a", "b" => 2, :c => 3})
p k1.select! { |k| k == "b" }
p k1

# the predicate is the block's last value, whatever its kind: nil and false
# reject, everything else keeps, including 0 and ""
t = gh({1 => nil, "b" => false, :c => 0, 4 => "", 5 => "x"})
p t.select! { |k, v| v }
p t
u = gh({1 => nil, "b" => 2, :c => "s"})
u.delete_if { |k, v| v }
p u

# a body of more than one statement, whose last is the predicate
w = gh({1 => "a", "b" => 2, :c => 3})
seen = []
w.reject! do |k, v|
  seen << k
  v.is_a?(Integer)
end
p w
p seen

# filter! in expression position: nil when nothing was removed, self otherwise
fl = gh({1 => "a", "b" => 2, :c => 3})
p fl.filter! { |k, v| true }
p fl.filter! { |k, v| k != 1 }
p fl

# a receiver that is a method's value, not a local, and numbered parameters
p gh({1 => "a", "b" => 2}).delete_if { |k, v| k == 1 }
np = gh({1 => "a", "b" => 2, :c => 3})
p np.select! { _1 == "b" || _2 == 3 }
p np.reject! { it == :c }

# a typed hash whose value is poly is read the same way, and so is a
# predicate whose value is a nilable Integer: its nil rejects, its 0 keeps
s = {"a" => 1, "b" => nil, "c" => "x"}
p s.select! { |k, v| v }
p s
y = {a: 1, b: nil}
y.delete_if { |k, v| v }
p y
pos = "ab"
n1 = {a: "a", b: "z"}
n1.select! { |k, v| pos.index(v.to_s) }
p n1
n2 = {a: "a", b: "z"}
p n2.reject! { |k, v| pos.index(v.to_s) }
p n2

# the filter as a method's value: what it answers, not nil
def keep_but_one(h)
  h.keep_if { |k, v| k != 1 }
end
def drop_twos(h) = h.reject! { |k, v| v == 2 }
p keep_but_one(gh({1 => "a", "b" => 2, :c => 3}))
p drop_twos(gh({1 => "a", "b" => 2}))
p drop_twos(gh({1 => "a", :c => 3}))

# `next` leaves the block with its value, or with nil, and the loop goes on
x1 = gh({1 => "a", :b => 2, "c" => 3})
p x1.select! { |k, v| next false if k == :b; true }
x2 = gh({1 => "a", :b => 2, "c" => 3})
p x2.reject! { |k, v| next true if k == :b; next if k == 1; false }
x3 = {"x" => 1, "y" => 2, "z" => 3}
p x3.select! { |k, v| next false if k == "y"; true }
x4 = gh({1 => "a", :b => 2})
p x4.select! { |k, v| next k == 1 }

# a key added during the iteration raises, as CRuby's does; a pair deleted
# during it -- the block's own, an earlier one, a later one -- is skipped
# and no other
ad = gh({1 => "a", :b => 2})
begin
  ad.reject! { |k, v| ad[99] = 1 if k == 1; k == :b }
rescue RuntimeError => e
  puts e.message
end
seen = []
dc = gh({1 => "a", 2 => "b", :c => 3, "d" => 4})
dc.select! { |k, v| seen << k; dc.delete(1) if k == 2; true }
p seen, dc
seen = []
dd = {1 => "a", 2 => "b", 3 => "c", 4 => "d"}
dd.delete_if { |k, v| seen << k; dd.delete(k) if k == 2; dd.delete(4) if k == 1; false }
p seen, dd
seen = []
de = {"a" => 1, "b" => 2, "c" => 3}
de.select! { |k, v| seen << k; de.delete(k) if k == "b"; false }
p seen, de

# nothing is removed from a frozen hash: the block never runs
f = gh({1 => "a", "b" => 2})
f.freeze
begin
  f.select! { |k, v| puts "ran"; true }
rescue FrozenError => e
  p e.class
end
begin
  f.delete_if { |k, v| puts "ran"; true }
rescue FrozenError => e
  p e.class
end
p f
