# A top-level method is emitted as sp_<name>, which is the runtime's own
# namespace. The mangler carries an rb_ infix for names that would land in it,
# but it only looked at the segment in front of an underscore -- so `def sym_x`
# was protected while `def sym` collided with the sp_sym TYPEDEF and the
# program failed to build on a C diagnostic that never named `sym`.
def sym(v) = "sym:#{v}"
def int(v) = "int:#{v}"
def float(v) = "float:#{v}"
def queue(v) = "queue:#{v}"
def thread(v) = "thread:#{v}"
def argv(v) = "argv:#{v}"
def fmod(v) = "fmod:#{v}"
def safepoint(v) = "safepoint:#{v}"
def sym_x(v) = "sym_x:#{v}"
def plain(v) = "plain:#{v}"

p sym(1)
p int(2)
p float(3)
p queue(4)
p thread(5)
p argv(6)
p fmod(7)
p safepoint(8)
p sym_x(9)
p plain(10)

# and the mangling does not leak into Ruby: the names still call each other
def outer(v) = sym(v) + "/" + int(v)
p outer(11)
