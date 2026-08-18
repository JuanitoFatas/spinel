# #450 cascade 2: a method like
#
#   def match_pattern(pattern, path)
#     pp = pattern.split("/")
#     p2 = path.split("/")
#     ...
#   end
#
# with no concrete-typed caller (or callers passing a still-int default
# from a deeper chain) used to leave `pattern` / `path` typed sp_int.
# `.split("/")` on a sp_int then failed C compile. Now the analyze
# pass infers "string" for params that have String-specific methods
# called on them in the body.

module Router
  def self.match_pattern(pattern, path)
    pp = pattern.split("/")
    p2 = path.split("/")
    return nil if pp.length != p2.length
    pp[0] + ":" + p2[0]
  end
end

puts Router.match_pattern("a/b/c", "x/y/z")

# Top-level def variant.
def normalize(s)
  s.strip.downcase
end

puts normalize("  HELLO  ")

# Param widened only when the called method is String-only. `length`
# alone (a container builtin) stays int — only flagging "definitive
# String evidence" methods keeps the inference conservative.
def show_len(arr)
  arr.length
end

puts show_len([1, 2, 3])
