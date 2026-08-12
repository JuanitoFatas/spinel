# Naming a bundled library's class without requiring it is a compile error
# that says which require is missing. This file shows the working side: the
# require present, and a user class of the same name unaffected.
require "stringio"

io = StringIO.new
io << "ok"
p io.string

class Set
  def initialize
    @a = []
  end

  def add(x)
    @a << x
    self
  end

  def size
    @a.size
  end
end

s = Set.new
s.add(1).add(2)
p s.size
