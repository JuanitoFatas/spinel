# Thread#kill terminates a thread, running its ensure blocks; Thread#raise
# injects an exception caught by the thread's rescue. A thread is blocked on a
# Queue so the delivery point is deterministic, and it announces itself on a
# second Queue first: CRuby's Thread.pass is only a hint, and on a loaded
# runner the child had not run a single statement by the time #kill arrived,
# so `log` came back empty (killing a never-started thread runs no ensure --
# the third case below documents exactly that). A blocking #pop is a
# handshake rather than a hint. This asserts the documented semantics, never
# a per-run interleaving.
Thread.report_on_exception = false

# #kill runs the ensure of a blocked thread
q = Queue.new
ready = Queue.new
log = []
t = Thread.new do
  begin
    log << :started
    ready << :go
    q.pop
    log << :unreached
  ensure
    log << :ensure_ran
  end
end
ready.pop              # the thread is inside the begin; nothing pushes to q
Thread.pass
t.kill
t.join
p log                  # [:started, :ensure_ran]
p t.alive?             # false

# #raise injects an exception the thread rescues
q2 = Queue.new
ready2 = Queue.new
r = Thread.new do
  begin
    ready2 << :go
    q2.pop
    "no"
  rescue => e
    "caught: #{e.message}"
  end
end
ready2.pop             # the thread is inside the begin, so its rescue is live
Thread.pass
r.raise("boom")
p r.value              # "caught: boom"

# killing a never-run thread just marks it dead (no body, no ensure)
u = Thread.new { q.pop }
u.kill
u.join
p u.alive?             # false

# #kill returns the thread; #exit / #terminate are aliases
v = Thread.new { q.pop }
p v.kill.equal?(v)     # true
v.join
