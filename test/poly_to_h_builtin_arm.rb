# The poly dispatch switch carries an arm per class that defines `to_h` and had
# none for a builtin, so a plain Hash reached the default and raised -- naming
# Hash, the class whose method it is. Every sibling (to_a, to_s, keys, length)
# already had its arm. It goes in the DEFAULT rather than as a pre-arm: a class
# that defines to_h has its own case and never gets there, and a Struct, whose
# to_h is generated rather than emitted as a method, does (#4170).
#
# The results are printed as sorted pair lists: Hash#inspect changed spelling
# in Ruby 3.4 and this file has to read the same under either.
class Box
  def to_h
    { z: 9 }
  end
end
Pt = Struct.new(:x, :y)

def poly(n)
  case n
  when 0 then { a: 1, b: 2 }
  when 1 then Box.new
  when 2 then [[:k, 1], [:m, 2]]
  when 3 then Pt.new(4, 5)
  else 7
  end
end

def pairs(h)
  h.to_h.map { |k, v| "#{k}=#{v}" }.sort
end

p pairs(poly(0))
p pairs(poly(1))
p pairs(poly(2))
p pairs(poly(3))
begin
  poly(4).to_h
rescue NoMethodError
  puts "NoMethodError"
end

# the shape the report came from: a helper opening with `opts.to_h.each`
def image_tag(opts)
  out = []
  opts.to_h.each { |k, v| out << "#{k}=#{v}" }
  out.sort
end
p image_tag(poly(0))
p image_tag(poly(1))

# and with no user class defining to_h at all
def only_builtin(n)
  n > 0 ? { q: 1 } : [[:r, 2]]
end
p only_builtin(1).to_h.keys
p only_builtin(0).to_h.keys
