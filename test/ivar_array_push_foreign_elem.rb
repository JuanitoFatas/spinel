# An array held in an instance variable takes a push of another type: the
# push-widening pass chose the poly ARRAY for the slot, but the write-merge
# unified the array kinds to the plain poly SCALAR, which boxed the slot and
# sent the push through sp_poly_shl -- where the foreign element was silently
# coerced to the array's own kind ([0, 0] for a pushed "one"). Two array
# kinds now merge to the poly array, the same slot the element-write arm
# already produces (#4196).
class Holder
  def initialize
    @a = [0]
  end

  def add(thing)
    @a.push(thing)
  end

  def answer
    puts @a.inspect
  end
end

holder = Holder.new
holder.add("one")
holder.answer

# The shove operator through the same shape.
class Shover
  def initialize
    @s = ["seed"]
  end

  def add(x)
    @s << x
  end

  def list
    @s
  end
end

sh = Shover.new
sh.add(42)
sh.add(nil)
p sh.list

# Two typed-array writes of different kinds into one ivar stay an array.
class Twin
  def initialize(f)
    @t = f ? (1..3).to_a : ["a", "b"]
  end

  def peek
    @t
  end
end

p Twin.new(true).peek
p Twin.new(false).peek
p Twin.new(true).peek.length

# An int-array ivar that only ever sees ints keeps its typed representation:
# push evidence of the same kind must not widen.
class Narrow
  def initialize
    @n = [1]
  end

  def add(v)
    @n.push(v)
  end

  def total
    @n.sum
  end
end

nr = Narrow.new
nr.add(2)
nr.add(3)
p nr.total
