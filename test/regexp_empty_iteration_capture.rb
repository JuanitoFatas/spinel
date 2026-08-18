# A repetition whose body can match empty runs one final iteration that
# consumes nothing, and that iteration is the one that ends the loop: the group
# inside it holds the empty string at the position the loop stopped at. The VM
# stopped one iteration earlier, so the group still held the text of the last
# iteration that consumed something. Ported from mruby-regexp (45c588a83).
p "a".match(/(a?)*/)[1]
p "a".match(/(a?)*/).begin(1)
p "aab".match(/(a*)*b/)[1]
p "a".match(/(a?)+/)[1]
p "b".match(/(a?)*/)[1]
p "b".match(/(a*)*b/)[1]
p "a".match(/(|a)*/)[0]
p "ab".split(/(a?)*/, -1)
p "ab".scan(/(a?)*/)

# a body that always consumes keeps its single-pass walk
p "aaa".match(/(a)*/)[1]
p "aaa".match(/(a)+/)[0]
p "abc".scan(/x?/).size
p "xayb".scan(/(a|b)/).flatten
p "aaa".gsub(/a*/, "-")
