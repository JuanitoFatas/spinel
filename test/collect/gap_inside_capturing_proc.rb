# SP_COLLECT_ERRORS=1 on this file used to SIGSEGV.
#
# Collect mode recovers from an unsupported construct with a longjmp back to
# the top-level unit loop. The emission globals are left wherever the abandoned
# unit put them, and several of them point INTO that unit's frame: g_cap_names
# at emit_proc_literal's capture NameSet, g_cap_struct and g_proc_return_home
# at its stack arrays, g_pre at whichever automatic Buf the emitter was filling.
#
# So: put a gap inside a proc that captures a local, then name a local in a
# LATER unit. `a` abandons with g_cap_names pointing at its dead frame; `b`'s
# emit_local_ref walks it. The crash site moved with the optimization level,
# which is what made it look like a bad pointer rather than corruption (#4141).
#
# Even without the capture-set collision the abandoned unit corrupted the next
# one silently: `b` came out assigning the proc return funnel instead of
# returning, because g_result_var survived the jump too.
def a
  x = "hi"
  f = proc { @@zz.to_s + x }
  f.call
end

def b(scheme)
  x = scheme
  "#{x}://host"
end

a
p b("http")
