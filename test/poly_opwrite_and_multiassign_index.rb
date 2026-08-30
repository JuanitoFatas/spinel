# Two emitter holes a widened program walked into (#4204, LangArena):
#
# 1. `@r += bump(1)` where @r is poly (Integer | user class defining `+`) and
#    bump's own operands are boxed: the RHS was emitted while the hoisted
#    temp's declaration line was half-written, splicing the call's argument
#    temps INTO the initializer (a declaration inside a declaration -- cc
#    stopped); and the numeric fallback arm re-emitted the RHS, running its
#    side effects twice. The RHS renders into a side buffer first now, and
#    both runtime arms fold the one evaluated temp.
#
# 2. `a[i], a[j] = a[j], a[i]` with a widened (boxed) index: the multiple-
#    assignment index target passed the sp_RbVal straight into the typed
#    set's sp_int slot. The index unboxes now, as the boxed-receiver branch
#    always did.
class I64
  attr_reader :v

  def initialize(v)
    @v = v
  end

  def +(o)
    I64.new(@v + (o.is_a?(I64) ? o.v : o))
  end

  def to_s = "I64(#{@v})"
end

class Acc
  attr_reader :calls

  def initialize
    @r = 0
    @calls = 0
  end

  def bump(x)
    @calls += 1
    return 0 if x.nil?
    x
  end

  def go
    @r += bump(1)
  end

  def upgrade
    @r = I64.new(100)
  end

  def out = @r
end

a = Acc.new
a.bump(nil)
a.go
a.go
p a.out
p a.calls
a.upgrade
a.go
puts a.out
p a.calls

# The swap with widened indexes.
arr = [10, 20, 30]
i = [0, "pad"][0]
j = [2, "pad"][0 * 2]
j = 2 if j == 0
arr[i], arr[j] = arr[j], arr[i]
p arr
