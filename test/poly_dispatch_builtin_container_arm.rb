# A call polymorphic over a builtin container and a user class that shadows the
# same method name lost the builtin arm. The non-yielding reads (first, last,
# keys, values) were pinned to the user class's return type and left every
# builtin tag on the dispatch's raise default, so a genuine Array or Hash
# raised NoMethodError. The yielding ones (map, select) did run the block, but
# the block was never classified as lifted, so its writes to enclosing locals
# went to a copy and were lost. #3459, following #3409.
class Shadow
  def first; nil; end
  def last; nil; end
  def keys; nil; end
  def values; nil; end
  def map; nil; end
  def select; nil; end
  def size; nil; end
  def length; nil; end
  def each; nil; end
end
class Src
  def self.hash_of(w)
    if w
      h = {}
      h["a"] = 1
      h
    else
      Shadow.new
    end
  end
  def self.arr(w)
    w ? [3, 1, 2] : Shadow.new
  end
end
a = Src.arr(true)
h = Src.hash_of(true)
p a.first
p a.last
p h.keys
p h.values
n = 0
a.map { |x| n = n + 1 }
p n
m = 0
a.select { |x| m = m + 1; x > 1 }
p m
p a.size
p a.length
p h.size
e = 0
a.each { |x| e = e + 1 }
p e
p a.map { |x| x * 2 }
p a.select { |x| x > 1 }
s = Src.arr(false)
p s.first
p s.map
