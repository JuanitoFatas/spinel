# A generated reader owns its name in CRuby, builtins included:
# Data.define(:to_s) answers the member, and only object_id draws a warning.
# Spinel's builtin arms claimed these first -- to_s/inspect through the
# generated stringifiers, freeze/dup/itself through the identity shortcut,
# hash/object_id splitting the two halves and stopping the build (#4190).
# A reader now outranks each of them, on both halves.
A = Data.define(:to_s);      p A.new("x").to_s
B = Data.define(:inspect);   p B.new("x").inspect
C = Data.define(:freeze);    p C.new("x").freeze
D = Data.define(:dup);       p D.new("x").dup
E = Data.define(:itself);    p E.new("x").itself
F = Data.define(:hash);      p F.new("x").hash
G = Data.define(:object_id); p G.new("x").object_id
H = Data.define(:display);   p H.new("x").display
I = Struct.new(:to_s);       p I.new("x").to_s
puts "#{A.new("y")}"
p B.new("z")
# a class with no such member keeps every builtin
Plain = Data.define(:v)
q = Plain.new(3)
p q.itself.v
p q.dup.v
p q.frozen? == q.freeze.frozen?
