# Two calls, each contradicting the seed for the method it calls. Without
# SP_COLLECT_ERRORS the run stops at the first, which is correct but costs a
# whole compile per signature on a tree that carries hundreds of them (#4140).
module Paths
  def self.a_path(id) = "/a/#{id}"
  def self.b_path(id) = "/b/#{id}"
end

puts Paths.a_path("token-1")
puts Paths.b_path("token-2")
