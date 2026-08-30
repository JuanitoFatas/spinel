# A user method whose name a builtin owns (Fiber#resume), dispatched through
# a poly slot, was unreachable when it takes KEYWORD arguments: the dispatch's
# candidate filter compared the call's positional count against nrequired,
# which counts required keyword params -- so the keyword call looked
# arity-impossible, every arm dropped, and the call lowered to the unresolved
# raise. A keyword hash now funds the declared keyword params it names, in
# both the candidate count and the per-arm filter (#4205).
class Session
  def resume(user_agent:, ip_address:)
    "resumed #{user_agent} #{ip_address}"
  end
end

box = [Session.new, "x"]
s = box[0]
puts s.resume(user_agent: "UA", ip_address: "1.2.3.4")

# Mixed positional + keyword through the same shape.
class Job
  def enqueue(name, priority:, retries: 0)
    "#{name} p#{priority} r#{retries}"
  end
end

j = [Job.new, 3][0]
puts j.enqueue("mail", priority: 2)
puts j.enqueue("sync", priority: 1, retries: 5)

# A receiver that is not the owner still raises, receiver named.
begin
  box[1].resume(user_agent: "UA", ip_address: "x")
rescue NoMethodError
  puts "raises for String"
end

# The positional twin keeps working beside it.
class Chair
  def resume(ua, ip)
    "sat #{ua} #{ip}"
  end
end

c = [Chair.new, "y"][0]
puts c.resume("a", "b")
