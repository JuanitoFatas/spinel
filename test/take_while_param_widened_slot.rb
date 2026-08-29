# A take_while/drop_while block parameter is an ordinary local, and its SLOT
# can widen to poly while the receiver stays a typed array -- a second method
# of the receiver's method's name being widened, plus a never-called caller
# chain, is enough. The loop then shadowed the sp_RbVal slot with a
# const char * of the element type, while the predicate read the parameter at
# the slot's type (sp_poly_truthy), and the C build stopped. The element now
# binds boxed into the slot's own type, the way a for-loop variable does since
# #4168 (#4188).
class Box
  def split(sep)
  end
end
Spec = Struct.new(:includes)

def root_of(pattern)
  pattern.split("/").take_while { |segment| segment }
end

def drop_of(pattern)
  pattern.split("/").drop_while { |segment| segment == "app" }
end

def body_reads(pattern)
  pattern.split("/").take_while { |segment| segment.length > 0 }
end

def walk(patterns)
  patterns.each { |pattern| root_of(pattern) }
end
def sweep(specs)
  specs.each { |spec| walk(spec.includes) }
end
def build
  Spec.new(["a/b"])
end

p root_of("app/**/*")
p drop_of("app/x/y")
p body_reads("a/b/")
# the untouched shapes stay untouched
p [1, 2, 0, 3].take_while { |n| n > 0 }
p ["x", "y"].take_while { |s| s }
