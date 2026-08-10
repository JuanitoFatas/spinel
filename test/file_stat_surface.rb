path = "/tmp/sp_stat_surface_#{Process.pid}"
File.write(path, "hello")
st = File.stat(path)
p st.size
p st.size?
p st.zero?
p st.nlink
p st.pipe?
p st.readable?
p st.writable?
p st.blockdev?
p st.chardev?
p(st.ino > 0)
p(st.blksize > 0)
p(st.uid == Process.uid)
p(st.gid == Process.gid)
st2 = File::Stat.new(path)
p st2.size
p st2.file?
File.delete(path)
