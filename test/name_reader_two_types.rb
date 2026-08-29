# `.name` on a boxed receiver has a builtin meaning -- a Class's name, a
# String -- guarded by "no user class defines the name". The guard checked
# METHODS only, and a Data/Struct member reader or an attr_reader is not a
# method the AST carries, so two generated `name` readers of different types
# were overridden to String and the Integer member was assigned raw into a
# const char * accumulator (#4189). The guard now asks defines-or-READS, the
# same predicate its to_s/inspect siblings use; with user readers present the
# call types as their union, and Class#name still answers where none exist.
Finding = Data.define(:name)
Declaration = Data.define(:name)
St = Struct.new(:name)

class Plain
  attr_reader :name
  def initialize(n) = @name = n
end

def spelled(one)
  one.name
end

p spelled(Declaration.new("x"))
p spelled(Finding.new(1))
p spelled(St.new(:sym))
p spelled(Plain.new(2.5))
p spelled(Finding.new(nil))

# Class#name must keep working through a poly slot when no user reader exists
def cls_of(x)
  x.class
end
p cls_of(1).name

# a Class flowing through the same slot as a reader-bearing object
Named = Data.define(:name)
def named_of(one) = one.name
p named_of(Named.new("v"))
p named_of(Integer)
