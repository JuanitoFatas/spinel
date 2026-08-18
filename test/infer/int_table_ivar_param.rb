# A table of int arrays kept on an ivar, read as `@t[k][j]` and fed to a helper.
# The narrowing that gives the table its typed representation used to run only
# after the fixpoint, so during parameter binding `@t[k][j]` was still poly --
# and parameters only widen, so the helper took sp_RbVal for good. Every
# signature below must stay sp_int.
module F
  P = 97
  def self.add(a, b) = (a + b) % P
  def self.sub(a, b) = (a - b) % P
end

class T
  def initialize(n)
    @n = n
    @t = Array.new(n) { Array.new(n, 0) }
    k = 0
    while k < n
      @t[k][(k + 1) % n] = F.add(@t[k][(k + 1) % n], 1)
      @t[k][k] = F.sub(@t[k][k], k + 1)
      k += 1
    end
  end

  def fingerprint
    s = 0
    r = 0
    while r < @n
      row = @t[r]
      c = 0
      while c < @n
        s = F.add(s, row[c])
        c += 1
      end
      r += 1
    end
    s
  end
end

puts T.new(12).fingerprint
