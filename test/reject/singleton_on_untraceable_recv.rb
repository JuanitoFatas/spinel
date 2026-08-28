# `def <recv>.m` is compiled by synthesizing a subclass of what <recv> holds, so
# the receiver has to be traceable to one `new` of a plain user class. When it
# is not -- a builtin handle from Socket.pair, a bare Object.new, a variable
# written more than once -- there is no subclass to attach the method to, and
# the body used to fall through to the ordinary def emitter: a plain function of
# the enclosing scope, with `self` undeclared and its `@ivars` read as the
# enclosing class's. Say so here rather than in the generated C (#4169).
sock = Object.new

def sock.close
  return if @closed
  @closed = true
end

sock.close
