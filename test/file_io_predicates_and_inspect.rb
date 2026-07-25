# File real-uid predicates, File.realdirpath, IO#inspect, String/IO.try_convert
p File.readable_real?("/etc/passwd")
p File.readable_real?("/nonexistent_spinel_xyz")
p File.writable_real?("/nonexistent_spinel_xyz")
p File.executable_real?("/nonexistent_spinel_xyz")
p File.readable?("/etc/passwd")
p File.readable?("/nonexistent_spinel_xyz")
p File.realdirpath("/etc")
p File.realdirpath("/etc/definitely_missing_name_xyz")
p File.realdirpath("passwd", "/etc")

f = File.open("/etc/passwd")
p f.inspect
f.close
p f.inspect
p STDIN.inspect

p String.try_convert("abc")
p String.try_convert(5)
p Integer.try_convert(5)

p Math.expm1(0)
p Math.log1p(0)
