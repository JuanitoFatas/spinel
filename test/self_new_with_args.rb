# `self.new` in a class method dispatched to the receiving class with no
# arguments and was refused with one: the zero-arg arm accepted the SelfNode
# receiver by shape while the argument-taking arm asked class_recv_is_dynamic,
# which did not know self. Both arms take it now (#4202).
class One
  def initialize(x)
    @x = x
  end

  def self.make(x)
    self.new(x)
  end

  def who = "One " + @x
end

class OneSub < One
  def who = "OneSub " + @x
end

puts One.make("a").who
puts OneSub.make("b").who

# The class-side template-method shape that found it: a base class method
# constructing `self.new(...)` and calling an overridable instance method.
class Filter
  def initialize(content)
    @content = content
  end

  def self.apply(content)
    filter = self.new(content)
    filter.applicable? ? filter.render : content
  end

  def applicable? = false
  def render = @content
end

class Loud < Filter
  def applicable? = true
  def render = @content.upcase
end

puts Filter.apply("hello")
puts Loud.apply("hello")

# Two arguments, and the implicit-self spelling.
class Pair
  def initialize(a, b)
    @a = a
    @b = b
  end

  def self.of(a, b)
    new(a, b)
  end

  def sum = @a + @b
end

class PairSub < Pair
  def sum = @a * @b
end

p Pair.of(2, 3).sum
p PairSub.of(2, 3).sum
