# A nilable scalar spells nil with a reserved sentinel, so every path that can
# carry one into a poly context has to box it as nil rather than as the number.
# The marking enumerates those paths by shape, and these were missing: a
# conditional used as an expression (its value is one of its arms), an ivar and
# the attr_reader over it, a constructor argument (no instance receiver, so the
# parameter binding could not see it), and an array element read back out or
# bound to a block parameter (#3505).
class NpR
  def initialize(p)
    @p = p
  end

  def p_
    @p
  end
end

r = NpR.new(nil)

def probe(k)
  h = {}
  h[k] = "from-path"
  h[nil] = "from-literal"
  [h.length, h[nil]]
end

# conditional arms
p probe(true ? r.p_ : 3)
p probe(if true then r.p_ else 3 end)
p probe(begin; r.p_; rescue; 3; end)
p probe(case 1 when 1 then r.p_ else 3 end)
p probe(r.p_ || 3)

# an if with no else arm is itself a nil
def maybe(r, c)
  if c then r.p_ end
end
p probe(maybe(r, true))
p probe(maybe(r, false))

# through an ivar and the reader over it, built by a constructor
class NpW
  attr_reader :v
  def initialize(v)
    @v = v
  end
end
p probe(NpW.new(r.p_).v)
w = NpW.new(r.p_)
p probe(w.v)

# through an array element, read back and bound to a block parameter
a = [r.p_, 7]
p probe(a[0])
seen = []
a.each { |k| seen << probe(k) }
p seen

b = []
b << r.p_
p probe(b[0])

# a value that is NOT nil keeps its number down every one of those paths
q = NpR.new(4)
p probe(true ? q.p_ : 3)
p probe(NpW.new(q.p_).v)
p probe([q.p_][0])
p probe(maybe(q, true))
