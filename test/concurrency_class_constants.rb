# The concurrency classes answer as VALUES, not just as constructors: naming
# one raised NameError even though .new worked. The top-level constant is
# CRuby's alias and #name answers the qualified form. Queue and SizedQueue are
# one runtime object told apart by its bound, so #class and the predicates read
# that rather than the static type -- a SizedQueue called itself a Queue. #3466.
p Queue, Mutex, Thread, Fiber, ConditionVariable, SizedQueue
p Queue.new.class
p SizedQueue.new(2).class
p Mutex.new.class
p Fiber.new { 1 }.class
p Mutex.name, Thread.name, SizedQueue.name
q = SizedQueue.new(2)
p q.max
p q.is_a?(SizedQueue), q.instance_of?(SizedQueue), q.is_a?(Queue)
u = Queue.new
p u.is_a?(SizedQueue), u.instance_of?(Queue), u.is_a?(Queue)
p SizedQueue.superclass, Queue.superclass, Mutex.superclass
p Fiber.ancestors.first(2)
