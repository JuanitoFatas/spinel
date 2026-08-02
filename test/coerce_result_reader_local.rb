# Reading an ivar off a coerce-only operator result and assigning it to a local
# aborted the C build: the reader was typed from the ivar slot as the analysis
# had it partway through, while the slot itself finished poly, so the emitted
# assignment put an sp_RbVal into an sp_Rational local. Rational and Complex are
# by-value structs, which is why an Integer or Float ivar survived the same
# shape. The local has to follow the slot it is read out of (#3500).
class Q
  attr_reader :v
  def initialize(v); @v = v; end
  def self.scalar(v); new(v); end
  def coerce(other); [Q.scalar(other), self]; end
  def *(other); Q.new(@v * other.v); end
end

v1 = (2 * Q.new(Rational(3, 2))).v
p v1
p v1.class

# the neighbouring forms that already worked stay working
p((2 * Q.new(Rational(3, 2))).v)
q1 = (2 * Q.new(Rational(3, 2)))
p q1.v
t = Q.new(Rational(5, 4))
p t.v
p (Q.new(Rational(1, 2)) * Q.new(Rational(2, 3))).v

class C
  attr_reader :v
  def initialize(v); @v = v; end
  def self.scalar(v); new(v); end
  def coerce(other); [C.scalar(other), self]; end
  def *(other); C.new(@v * other.v); end
end

v2 = (2 * C.new(Complex(1, 2))).v
p v2
p v2.class
p((2 * C.new(Complex(1, 2))).v)

# a scalar ivar takes the same route
class I
  attr_reader :v
  def initialize(v); @v = v; end
  def self.scalar(v); new(v); end
  def coerce(other); [I.scalar(other), self]; end
  def *(other); I.new(@v * other.v); end
end

p (2 * I.new(4)).v
p (2.0 * I.new(2.5)).v
