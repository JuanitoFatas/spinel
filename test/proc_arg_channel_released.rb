# The boxed-argument side channel a proc call publishes through is a GC root.
# Holding the last call's arguments there kept whatever they pointed at alive
# for the rest of the program, so a dropped structure was re-marked at every
# collection. The callee reads the channel into its parameters and clears it.
def build(n)
  a = []
  n.times { |i| a << [i, i] }
  a
end

sink = proc { |x| x.size }

def feed(sink)
  big = build(120_000)
  sink.call(big) == 120_000
end

base = GC.stat["bytes"]
p feed(sink)
GC.start
GC.start
# the dropped structure is gone, not merely unreferenced by the program
p GC.stat["bytes"] < base + 4_000_000

# the arguments themselves still arrive intact, including several of them,
# a rest parameter and a block's own call
two = proc { |a, b| [a, b] }
p two.call("x", [1, 2])
rest = proc { |*a| a }
p rest.call(1, "b", :c)
nested = proc { |v| two.call(v, v) }
p nested.call(7)
