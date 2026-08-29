# Array#select! / #filter! / #reject! / #keep_if / #delete_if with a block
# that leaves early: `next` leaves the block with its value, or with nil, and
# the loop goes on to the next element; `break` leaves the loop with its
# value, as do a raise, a return and a throw, and the elements the block
# never saw stay in the array, after the ones it kept. On every array kind,
# and on a receiver that is a method's value, with a block that allocates.
def mk(n)
  a = []
  n.times { |i| a << i }
  a
end

# next with a value, and bare
p [1, 2, 3].select! { |x| next true if x == 2; false }
p [1, 2, 3].reject! { |x| next false if x == 2; true }
p %w[a bb ccc].delete_if { |s| next false if s == "bb"; s.size > 2 }
p [1.5, 2.5, 3.5].keep_if { |f| next true if f > 3; false }
p [:a, :b, :c].filter! { |s| next true if s == :b; false }
p [1.5, 2.5, 3.5].keep_if { |f| next if f > 3; true }
p [:a, :b, :c].filter! { |s| next s == :b }
m = [1, "b", :c, 4.0, nil]
p m.select! { |e| next true if e.nil?; e.is_a?(Integer) }
p m
S = Struct.new(:a)
st = [S.new(1), S.new(2), S.new(3)]
p st.select! { |s| next true if s.a == 2; false }
# next as the block's last statement, and inside a nested if
p [1, 2, 3, 4].select! { |x| if x.even? then next x > 2 end; true }

# break: the value is the call's, the array keeps what was kept and what
# was never seen
a = [1, 2, 3, 4, 5]
r = a.select! { |x| break if x == 3; x > 1 }
p r, a
b = [1, 2, 3, 4]
p b.reject! { |x| break 42 if x == 3; x == 1 }
p b
s = %w[x yy zzz w]
p s.delete_if { |t| break t if t == "zzz"; t.size == 1 }
p s
q = [1, "b", :c, 4]
p q.keep_if { |e| break e if e == :c; e.is_a?(Integer) }
p q
fl = [1.5, 2.5, 3.5, 4.5]
p fl.keep_if { |x| break if x == 3.5; x > 2 }
p fl
sy = [:a, :b, :c, :d]
p sy.filter! { |s| break s if s == :c; s != :a }
p sy
# next and break in one block
nb = [1, 2, 3, 4, 5]
p nb.reject! { |x| next false if x == 2; break 0 if x == 4; x < 3 }
p nb

# a raise, a return and a throw leave the array the same way: what was
# kept, the element the block was given, and the ones it never saw
x = [1, 2, 3, 4]
begin
  x.delete_if { |n| raise "boom" if n == 3; n == 2 }
rescue RuntimeError => e
  puts e.message
end
p x
def early(a)
  a.select! { |n| return n if n == 3; n.odd? }
  :never
end
y = [1, 2, 3, 4, 5]
p early(y), y
z = %w[a b c d]
v = catch(:found) { z.reject! { |s| throw :found, s if s == "c"; s == "a" } }
p v, z
w = [1, 2, 3, 4]
begin
  w.keep_if { |n| Integer("x") if n == 2; true }
rescue ArgumentError
  p w
end

# a frozen receiver is refused before the block runs; a block that writes to
# its receiver at the element it was given keeps what it wrote, and a
# predicate whose value is an Enumerator is truthy
fr = [1, 2, 3].freeze
begin
  fr.select! { |x| puts "ran"; true }
rescue FrozenError => e
  p e.class
end
p fr
wr = [1, 2, 3, 4]
wr.select! { |x| wr[wr.index(x)] = x * 10 if x == 2; true }
p wr
p [1, 2].select! { |x| x.to_s.each_char }

# the nil-or-self answers are as before
p [1, 2, 3].select! { |x| x > 0 }
p [1, 2, 3].reject! { |x| x > 5 }
p [1, 2, 3].filter! { |x| x > 1 }
p [1, 2].keep_if { |x| false }
p [1, 2].delete_if { |x| false }

# a receiver that is a method's value, with a block that allocates
p mk(40).select! { |x| "s#{x}".size > 1 && x.even? }
p mk(40).reject! { |x| [x].map { |y| y.to_s }.first.size > 1 }
zz = mk(6)
zz.delete_if { |x| zz = nil; x.odd? }
p zz
