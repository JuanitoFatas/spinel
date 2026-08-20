# StringIO's iteration surface. Two things were missing: the methods
# themselves, and -- underneath -- the ability for a native class's own
# methods to be called on an implicit self at all, which is how these are
# written. Every one of those was a NameError:
#
#   class StringIO
#     def rest = gets    # undefined local variable or method 'gets'
#   end
require "stringio"

io = StringIO.new("ab\ncd\n")
io.each_line { |l| p l }
p io.eof?

StringIO.new("x\ny\n").each { |l| p l }
StringIO.new("a|b|").each_line("|") { |l| p l }
StringIO.new("abc").each_char { |ch| p ch }
StringIO.new("ab").each_byte { |b| p b }

# each_line answers the receiver, and picks up where the stream is
io2 = StringIO.new("1\n2\n3\n")
p io2.gets
p io2.each_line { |l| }.class
p io2.eof?

# the implicit-self resolution this rests on, on its own
class StringIO
  def rest
    out = []
    while (line = gets)
      out << line
    end
    out
  end
  def at_end? = eof?
  def where = pos
end
io3 = StringIO.new("p\nq\n")
p io3.rest
p io3.at_end?
p io3.where
