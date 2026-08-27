# String mutators on a receiver whose class is only known at run time, and
# the names String and Array share. Covers the String-only mutators writing
# back through the box (setbyte, scrub!, bytesplice, append_as_bytes); the
# shared names (concat, prepend, reverse!, slice!) on a parameter that is a
# String at one call site and an Array at another, picking the arm by the
# receiver's run-time kind; the result flowing into a slot the inference
# widened to poly; the TypeError an argument of the other kind raises; and
# the NoMethodError a receiver of neither kind raises.
s = "seed"
s = ["hello".dup, 42][0]
p s.setbyte(0, 72)
p s.scrub!
p s.bytesplice(1, 1, "a")
p s.append_as_bytes(" world")
p s

def tack(x, y)
  x.concat(y)
end
def lead(x, y)
  x.prepend(y)
end
def flip(x)
  x.reverse!
end
def cut(x, i, n)
  x.slice!(i, n)
end
def head(x)
  x.slice!(0)
end

p tack("ab".dup, "c")
p tack([1, 2], [3])
p lead("ab".dup, ">")
p lead([1, 2], 0)
p flip("abc".dup)
p flip([1, 2, 3])
p cut("abcdef".dup, 1, 3)
p cut([1, 2, 3, 4], 1, 2)
p head("xyz".dup)
p head([7, 8, 9])

# the value keeps the receiver's kind: a String's answer is a String
t = tack("q".dup, "r")
p t.upcase
u = flip([4, 5])
p u.sum

# a slot that held a typed value on an early pass and the box on the last
v = [9, 8]
v = [[7, 6], 5][0]
p v.reverse!

begin
  x = [[1], 2][0]
  x.concat("z")
rescue TypeError => e
  puts e.message
end
begin
  lead("ab".dup, [1])
rescue TypeError => e
  puts e.message
end
begin
  flip(5)
rescue NoMethodError => e
  puts e.message
end

# the receiver's variable takes the new contents back whatever the mutator
# answers -- the removed part, the scrubbed text -- and so does an
# instance variable
w = ["abcdef".dup, 1][0]
p w.slice!(1, 3)
p w
z = ["a\xffb".dup, 1][0]
p z.scrub!
p z
@q = ["ab".dup, 0][0]
@q.concat("c")
@q.prepend(">")
p @q.reverse!
p @q

# an argument of a kind that is never the receiver's own is named as CRuby
# names it, whether the kind is known when the program is compiled (a
# Boolean is true or false only when it runs) or only when it runs
begin
  x.concat(1 > 0)
rescue TypeError => e
  puts e.message
end
begin
  w.concat(nil)
rescue TypeError => e
  puts e.message
end
[true, nil, 1.5].each do |v|
  begin
    tack(x, v)
  rescue TypeError => e
    puts e.message
  end
end
