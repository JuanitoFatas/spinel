# Issue #867: String#match and Regexp#match return a MatchData-like
# object instead of falling through to unresolved-call emit-0.
md1 = "hello world".match(/\w+/)
puts md1[0]

md2 = /(\w+) (\w+)/.match("hello world")
puts md2[0]
puts md2[1]
puts md2[2]

md3 = "a".match(/(a)(b)?/)
puts md3[2].nil? ? "nil capture" : "bad capture"
puts "abc".match(/z/).nil? ? "nil match" : "bad match"

# The widest pattern the match registers hold is 31 groups of its own, group 0
# making 32. This used to be 33 groups, which COMPILED and was then truncated
# here: length answered 32 where CRuby answers 34, and the last two groups read
# nil. A pattern past the ceiling is refused now (see RE_MAX_CAPTURES), so this
# is the widest one that reads, and every group of it must.
md4 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".match(/(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)/)
puts md4.length
puts md4[31]
puts md4[1]
puts md4.captures.length
puts md4.captures.uniq.inspect
