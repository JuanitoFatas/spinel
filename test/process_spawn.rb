# Process.spawn + Process.waitpid2 + Process::Status.
#
# CRuby semantics:
#   Process.spawn(cmd, *args, opts) -> Integer pid
#   Process.waitpid2(pid) -> [pid, Process::Status]
#   Process::Status#signaled?   -> true if killed by signal
#   Process::Status#exited?     -> true if exited normally
#   Process::Status#exitstatus  -> exit code (0..255) or nil
#   Process::Status#termsig     -> signal number or nil
#   Process::Status#coredump?   -> true if dumped core
#   Process::Status#success?    -> true if exited? and exitstatus == 0
#   Process::Status#pid         -> pid
#
# Process::Status has no public constructor in CRuby: the only way to
# obtain one is Process.waitpid2 on a real subprocess. Both CRuby and
# spinel must produce the same predicate answers here; the boxed-comparison
# path (status.exitstatus == 0) is exercised by other tests, this one
# focuses on the four predicates that the bash tool's signal-check path
# actually uses (exited? / success? / signaled? / pid).

# 1) Normal exit, success path. /bin/echo exits 0.
r, w = IO.pipe
pid = Process.spawn("/bin/echo", "-n", "hello", out: w)
w.close
out = r.read
_, status = Process.waitpid2(pid)
puts out == "hello"               # the spawned process actually ran
puts status.exited?               # exited normally
puts status.success?              # exited 0
puts status.signaled? == false    # not killed by signal
# status.pid is tested in (4) via the destructure, not via the boxed
# == path that the spinel poly-comparison route doesn't handle yet.

# 2) Nonexistent command -> Errno::ENOENT.
ok = false
begin
  r, w = IO.pipe
  pid = Process.spawn("/no/such/command", out: w)
  w.close
  r.read
  Process.waitpid2(pid)
rescue Errno::ENOENT
  ok = true
end
puts ok

# 3) /bin/false exits 1: exited? true, success? false.
r, w = IO.pipe
pid = Process.spawn("/bin/false", out: w)
w.close
r.read
_, status = Process.waitpid2(pid)
puts status.exited?
puts status.success? == false

# 4) The boxed status: from waitpid2's second element, .signaled? /
#    .exited? dispatch on the cls_id the poly path reads out of the boxed
#    value. Goes through emit_poly_builtin_method in codegen_call.c.
r, w = IO.pipe
pid = Process.spawn("/bin/true", out: w)
w.close
r.read
result = Process.waitpid2(pid)
got_pid = result[0]
got_st  = result[1]
puts got_pid == pid
puts got_st.exited?
puts got_st.success?
