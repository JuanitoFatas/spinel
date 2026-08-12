# `xs.each(&proc)` on a receiver whose type is only known at run time drove an
# empty loop body: the appends simply vanished.
def poly(n)
  n > 0 ? [5, 6] : nil
end

out = []
k = proc { |v| out << v }
poly(1).each(&k)
p out

def run(xs)
  acc = []
  xs.each do |row|
    h = proc { |v| acc << v }
    row.each(&h)
  end
  acc
end

p run([[1], [2, 3]])

# the typed receivers keep working
t = []
g = proc { |v| t << v * 2 }
[1, 2].each(&g)
p t

# each_with_index through the same path
seen = []
w = proc { |v, i| seen << "#{i}:#{v}" }
poly(1).each_with_index(&w)
p seen
