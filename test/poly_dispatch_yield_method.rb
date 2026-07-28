# A yielding method reached through a poly receiver.
#
# Blocks are fused, not passed: with a known receiver the method body is
# inlined at the call site with the block spliced into the yield. That leaves
# no symbol to call, so a poly dispatch dropped the arm and raised
# NoMethodError naming a class that plainly defines the method -- and with it
# went the whole block API of any class reached through a union.
#
# Such a method now also gets a second, ordinary definition taking the block as
# an sp_Proc *, with `yield` lowered to a call on it. Monomorphic call sites
# keep the splice; only the poly path pays for the closure.
#
# Two shapes are deliberately NOT served, and keep raising rather than answer
# wrongly (see docs/limitations.md): a yield nested inside a block of its own
# method, and a method whose poly call sites pass blocks of differing result
# type -- one shared function cannot carry both.

class A
  def go
    yield 5
  end

  def guarded
    block_given? ? yield(1) : 99
  end

  def early
    yield 1
    return 42
  end

  def withargs(a, b)
    yield a + b
  end

  def twice
    yield(1) + yield(2)
  end
end

class B
  def go
    0
  end

  def guarded
    -1
  end

  def early
    -3
  end

  def withargs(a, b)
    a - b
  end

  def twice
    -4
  end
end

def pick(n)
  n == 1 ? A.new : B.new
end

p pick(1).go { |v| v % 2 }
p pick(2).go { |v| v % 2 }

p pick(1).guarded { |v| v + 100 }
p pick(1).guarded
p pick(2).guarded

p pick(1).early { |v| v }
p pick(2).early { |v| v }

p pick(1).withargs(3, 4) { |s| s * 10 }
p pick(2).withargs(3, 4) { |s| s * 10 }

p pick(1).twice { |v| v * 3 }
p pick(2).twice { |v| v * 3 }

# a monomorphic call to the same method still fuses
a = A.new
p a.go { |v| v + 1 }
p a.twice { |v| v }
