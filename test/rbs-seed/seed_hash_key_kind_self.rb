# The same key-kind contradiction as seed_hash_key_kind.rb, reached through the
# call shape that escaped the check: a RECEIVERLESS call from one method of a
# class to another method of the same class. The contradiction rule resolves a
# bare `name(...)` through the free-function table, which holds only top-level
# scopes, so `takes_str(h)` inside `P.takes_sym` resolved to nothing and the
# argument was never judged.
#
# Unjudged, the seeds are both honoured: `h` is a `sp_SymPolyHash *` and the
# parameter is a `sp_StrPolyHash *`, so the emitted call passed one pointer as
# the other. clang only warns about that, but gcc 14 made
# -Wincompatible-pointer-types an error, so it surfaced as a build failure
# rather than as the Symbol-dereferenced-as-char* crash of #3975.
#
# Not a snapshot test -- failing to compile is the passing outcome, so the
# Makefile runs it and asserts the diagnostic.
class P
  def self.takes_str(h)
    puts h.size
  end

  def self.takes_sym(h)
    takes_str(h)
  end
end

P.takes_sym({ a: 1 })
