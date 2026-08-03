# A receiverless `eval(x)` was taken for Kernel#eval and refused outright, so a
# class that defines its own `eval` could not call it from inside itself. That
# is the natural name for a tree-walking interpreter's visit, and refusing it
# refuses the program the method belongs to. Kernel#eval on a runtime string is
# still refused: it is the one AOT cannot do.
class Interp
  def initialize
    @visits = 0
  end

  def eval(node)
    @visits += 1
    if node.is_a?(Array)
      eval(node[0]) + eval(node[1])
    else
      node * 2
    end
  end

  def visits
    @visits
  end
end

i = Interp.new
p i.eval(3)
p i.eval([1, 2])
p i.eval([[1, 2], 3])
p i.visits

# an explicit receiver resolves the same way
p i.eval(5)
p Interp.new.eval([4, 5])

# a subclass inherits it
class Sub < Interp
  def twice(n)
    eval(n) + eval(n)
  end
end
p Sub.new.twice(6)
