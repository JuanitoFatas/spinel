# IO.for_fd(fd, autoclose: false): the flag was set and never read, so
# closing the wrapper closed the caller's descriptor anyway. The wrapper
# holds a dup(2) of the fd now (offset and flags shared through the one
# file description), #fileno still answers the caller's own fd, and a
# missing mode is derived from the descriptor's access mode -- the fixed
# "r" default made fdopen fail outright on a write-only fd (#4208).
path = "/tmp/spinel-autoclose-#{Process.pid}.txt"

fd = IO.sysopen(path, File::WRONLY | File::CREAT | File::TRUNC, 0o644)
io = IO.for_fd(fd, autoclose: false)   # no mode: derived from the fd
p io.fileno == fd
p io.autoclose?
io.write "one "
io.close                                # must NOT close fd

io2 = IO.for_fd(fd, "w", autoclose: false)
io2.write "two"
io2.close

IO.for_fd(fd).close                     # autoclose default: really closes fd
p File.read(path)

# The runtime setter spelling: flip autoclose off after wrapping the fd
# itself, then close -- the descriptor survives for a second wrapper.
fd2 = IO.sysopen(path)                  # default flags: O_RDONLY
io3 = IO.for_fd(fd2)
io3.autoclose = false
p io3.autoclose?
io3.close
io4 = IO.for_fd(fd2)
p io4.read
io4.close

File.delete(path)
puts "done"
