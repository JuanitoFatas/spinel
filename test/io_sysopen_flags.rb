# IO.sysopen dropped its flags argument: every sysopen was O_RDONLY, so a
# FIFO opened for writing hung waiting for a writer of its own, and a
# write-mode open of a fresh file failed. The flags (and the optional perm)
# ride through now, and the errno maps to its own Errno class (#4206).
d = "/tmp/spinel-sysopen-#{Process.pid}"
Dir.mkdir(d)
begin
  path = File.join(d, "sys.txt")

  # O_WRONLY | O_CREAT | O_TRUNC = 1 | 64 | 512 on Linux
  fd = IO.sysopen(path, 1 | 64 | 512, 0o644)
  io = IO.for_fd(fd, "w")
  io.write "written by sysopen\n"
  io.close
  puts File.read(path)

  # default flags stay O_RDONLY
  fd2 = IO.sysopen(path)
  io2 = IO.for_fd(fd2, "r")
  puts io2.read
  io2.close

  begin
    IO.sysopen(File.join(d, "absent.txt"))
  rescue Errno::ENOENT
    puts "ENOENT"
  end
ensure
  File.delete(File.join(d, "sys.txt")) if File.exist?(File.join(d, "sys.txt"))
  Dir.rmdir(d)
end
