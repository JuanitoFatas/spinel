# `&&` / `||` with a concurrency handle on the left emitted a ternary whose
# arms had different C types, so the program did not build at all; with the
# handle on the right it built but the result lost its class, and a method on
# it raised NoMethodError naming nothing (#3484). The handle boxes like any
# other nullable builtin pointer now, and a boxed one names its own class --
# Monitor and SizedQueue included, which share a representation with Mutex
# and Queue.
m = Mutex.new
p(m.lock && m.locked?)
m.unlock

q = Queue.new
sq = SizedQueue.new(2)
cv = ConditionVariable.new
f = Fiber.new { 1 }
t = Thread.new { 1 }
t.join

p (:ok && q).class
p (:ok && sq).class
p (:ok && m).class
p (:ok && cv).class
p (:ok && f).class
p (:ok && t).class

p (q || :ok).class
p (m || :ok).class
p (f || :ok).class

p (m && 1)
p (f || 2).inspect.start_with?("#<Fiber:")
p (q && :done)

# the handle on the left with each of the other operand shapes
p (m && "s")
p (m && 1.5)
p (m && [1])
p (m && nil).nil?
p (m && m.locked?)

# a boxed handle names itself in inspect rather than answering #<Object>
p (q || 2).inspect.start_with?("#<Thread::Queue:")

# a falsy left keeps the left, and a nil-valued handle reads falsy
p (nil && q).nil?
p (nil || q).class
