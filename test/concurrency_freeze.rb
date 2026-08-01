# A Mutex, ConditionVariable, Fiber or Thread is an ordinary heap instance and
# freezes like one -- `freeze` used to be a no-op that left `frozen?` false
# right after it (#3483). A Queue is the exception Ruby itself makes: freezing
# one raises, since a frozen queue could never be pushed to again.
a = Mutex.new
p a.frozen?
p a.freeze.equal?(a)
p a.frozen?

b = ConditionVariable.new
b.freeze
p b.frozen?

c = Fiber.new { 1 }
c.freeze
p c.frozen?

t = Thread.new { 1 }
t.join
t.freeze
p t.frozen?

o = Object.new
o.freeze
p o.frozen?

q = Queue.new
p q.frozen?
begin
  q.freeze
  p "no raise"
rescue TypeError => e
  p [e.class, e.message.start_with?("cannot freeze #<Thread::Queue:")]
end

s = SizedQueue.new(1)
begin
  s.freeze
  p "no raise"
rescue TypeError => e
  p [e.class, e.message.start_with?("cannot freeze #<Thread::SizedQueue:")]
end

# a frozen handle still works as itself
m = Mutex.new
m.freeze
m.lock
p m.locked?
m.unlock
p m.frozen?
