# An embedded NUL is a byte of the string: the representation has always
# carried it (the header records the true byte length), but an operation that
# reached for strlen or strstr stopped at it and silently truncated. The policy
# is opportunistic -- fix what is met -- and this is what a sweep of the common
# String surface met.
s = "a\0b"
p s.bytesize
p s.size
p s == "a\0b"
p s == "a"
p s.bytes

p s.upcase.bytesize
p s.downcase.bytes
p s.reverse.bytes

p s.sub("\0", "-")
p s.sub("a", "X")
p s.gsub("\0", "-")
p s.gsub("a", "X")
p "abcabc".gsub("b", "-")
p "abc".gsub("", "-")

p s.split("\0").map(&:bytesize)
p s.split("").map(&:bytesize)
p "a\0b\0c".split("\0").size
p "a,b,c".split(",")

p [s].join.bytesize
p [s, s].join("\0").bytesize
p [1, 2].join("\0").bytesize
p ["x", "y"].join("-")

p s.tr("\0", "-")
p s.delete("\0").bytesize
p s.count("\0")
p "a\0\0b".squeeze("\0").bytesize
p "aabb".squeeze
p "abc".tr("b", "-")

# and the operations that already carried it
p s.b.bytesize
p s.dup.bytesize
p (+s).bytesize
p ("a\0b" + "c\0d").bytesize
p (s * 2).bytesize
p s.include?("\0")
p s.index("b")
p s.start_with?("a\0")
p s.end_with?("\0b")
p s.hash == "a\0b".hash
p({ s => 1 }[s])
p s.inspect
