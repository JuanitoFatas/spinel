# Kernel#Array asks a user object for #to_ary, then #to_a (#3721) -- but that
# arm ran only when the call site could statically see the class. Through an
# untyped parameter the argument reached sp_kernel_array, which wrapped the
# object whole: one object, one method, two answers, decided by the route. The
# runtime now asks the same two hooks in the same order, and a hash or range
# arriving the same way enumerates as the static arm does (#4187).
class Box
  def to_ary
    [1, 2, 3]
  end
end

class Bag
  def to_a
    [4, 5, 6]
  end
end

def wrap(x)
  Array(x)
end

p wrap(Box.new)
p wrap(Bag.new)
p Array(Box.new)
p Array(Bag.new)
p wrap(nil)
p wrap(7)
p wrap([8, 9])
p wrap({ a: 1 })
p wrap((1..3))
class Neither
  def to_s = "n"
end
p wrap(Neither.new).length
