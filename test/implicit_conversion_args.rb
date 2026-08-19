# CRuby's implicit conversion protocol at builtin argument boundaries: a user
# object defining #to_str / #to_int converts where a String / Integer is
# wanted; one that defines neither raises CRuby's TypeError. The raw object
# pointer previously went into the typed C slot and stopped the generated-C
# build.
class Stringish
  def to_str
    "conv-target.txt"
  end
end

class Mode
  def to_str
    "w"
  end
end

class Idx
  def to_int
    1
  end
end

class Inert
end

path = "conv-target.txt"
begin
  # to_str at File path and mode slots (with and without a block)
  File.open(Stringish.new, Mode.new) { |f| f.write("hello\n") }
  p File.read(Stringish.new)
  p File.readlines(Stringish.new)
  p File.exist?(Stringish.new)
  p File.file?(Stringish.new)

  # to_int at index and count slots
  p [10, 20, 30][Idx.new]
  p [10, 20, 30].take(Idx.new)
  p [10, 20, 30].first(Idx.new)
  p "abc".getbyte(Idx.new)

  # to_str at string-method argument slots
  p "hello world".include?(Mode.new.to_str)
  p "wow".delete(Mode.new)
  p "hello=world".partition(Mode.new.to_str + "orl")

  # no conversion method: CRuby's TypeError
  begin
    File.read(Inert.new)
  rescue TypeError => e
    p [e.class, e.message]
  end
  begin
    [10, 20, 30].take(Inert.new)
  rescue TypeError => e
    p [e.class, e.message]
  end
ensure
  File.delete(path) if File.exist?(path)
end

# the runtime bridge: a BOXED user object converts inside a generic runtime
# walk (pack), including a conversion method that arrives through a mixin
module IntLike
  def to_int
    7
  end
end

class Mixed
  include IntLike
end

p [Idx.new].pack("C").bytes
p [Mixed.new, Idx.new].pack("C2").bytes
begin
  [Inert.new].pack("C")
rescue TypeError => e
  p [e.class, e.message]
end
begin
  [nil].pack("C")
rescue TypeError => e
  p [e.class, e.message]
end
p [2**64 + 65].pack("Q")[0]

# native package bindings: a declared :string argument converts too
require "stringio"
p StringIO.new(Stringish.new).read
