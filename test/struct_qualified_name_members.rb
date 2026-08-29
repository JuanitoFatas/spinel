# A Struct or Data is constructed, not called: its arguments type the MEMBERS.
# That was done for the short name (`A.new`) and for an anonymous struct in a
# local, but a receiver written as a qualified path (`M::A.new`) took the
# ordinary bind-the-ctor-params road instead, so every member of a struct only
# ever built through its qualified name stayed untyped -- and a method reading
# one answered untyped, which left `to_s` out of the object dispatch and
# interpolation printing the builtin representation (#4185).
module M
  A = Struct.new(:x) do
    def to_s = x
  end
  module N
    B = Data.define(:y, :z) do
      def to_s = "#{y}/#{z}"
    end
  end
end
Top = Data.define(:t) do
  def to_s = t
end

def f(v) = "#{v}"

puts f(M::A.new("a"))
puts f(M::N::B.new("p", 2))
puts f(Top.new("q"))
puts f("s")

# keyword construction through a qualified name
puts f(M::N::B.new(y: "k", z: 9))
# a member the construction leaves out
C = Struct.new(:u, :w)
p C.new("only").w
