# String#count/#delete/#squeeze with a second set, and Hash#store, on a
# parameter inferred as untyped: the single-argument forms answered and the
# multi-argument forms raised NoMethodError -- the rows of #4149's table with
# a second argument (#4195). Covered with and without a user class owning the
# name, since the two shapes fail on different paths (the unresolved-call
# gate, and the dispatch switch's default).
def counted(text)
  a = "a"
  text.count(a)
end

def counted_between(text)
  a = "a"
  b = "b"
  text.count(a, b)
end

def counted_overlap(text)
  text.count("ab", "b")
end

def deleted(text)
  text.delete("ab", "b")
end

def squeezed(text)
  text.squeeze("a", "ab")
end

def stored(h)
  h.store("k", 1)
end

p counted(true ? "abcabc" : nil)
p counted_between(true ? "abcabc" : nil)
p counted_overlap(true ? "abcabc" : nil)
p deleted(true ? "aabbcc" : nil)
p squeezed(true ? "aaabbbccc" : nil)
h = (true ? { "x" => 9 } : nil)
p stored(h)
p h["k"]
p h["x"]

# A receiver that is not a string still raises, receiver named.
begin
  counted_between(true ? 7 : nil)
rescue NoMethodError => e
  puts "count raises for Integer"
end
begin
  stored(true ? 7 : nil)
rescue NoMethodError
  puts "store raises for Integer"
end

# A user class owning the name answers for its instances, and the string
# (or hash) still answers for itself through the same call.
class Tally
  def count(a, b)
    :tallied
  end

  def delete(a, b)
    :dropped
  end

  def store(k, v)
    "L:#{k}=#{v}"
  end
end

def go(x)
  x.count("a", "b")
end

def del(x)
  x.delete("ab", "b")
end

def put(x)
  x.store("q", 5)
end

p go(Tally.new)
p go("abcabc")
p del(Tally.new)
p del("aabbcc")
p put(Tally.new)
h2 = (true ? { "a" => 1 } : nil)
p put(h2)
p h2["q"]
