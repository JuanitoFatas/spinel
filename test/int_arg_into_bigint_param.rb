# A parameter that sees both a small Integer and a Bignum types as bigint --
# ty_unify keeps them together rather than widening to poly, on the premise
# that an int is promoted at the boundary.
#
# The promotion was only ever wired into the arithmetic operands, so a
# parameter that reached bigint any other way got an mrb_int handed to an
# sp_Bigint* slot: ill-typed C, caught only by -Werror.

def any(x)
  x.to_s
end
p any(1)
p any(2**200)

def arith(n)
  n * 2 + 1
end
p arith(3)
p arith(2**70)

def cmp(a, b)
  a > b
end
p cmp(5, 2**80)
p cmp(2**80, 5)

# the default-value path binds the same slot
def opt(x = 7)
  x.to_s
end
p opt
p opt(2**100)

# keyword and splat arguments reach it too
def kw(x:, y: 3)
  (x + y).to_s
end
p kw(x: 1)
p kw(x: 2**90, y: 2)

def rest(*xs)
  xs.map { |v| v.to_s }
end
p rest(1, 2**70)

# a constructor argument is the same binding
class Box
  def initialize(v)
    @v = v.to_s
  end

  def show
    @v
  end
end
p Box.new(9).show
p Box.new(2**90).show
