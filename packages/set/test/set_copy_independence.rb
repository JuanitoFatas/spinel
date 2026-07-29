# dup and clone produce a Set with its own storage: mutating the copy must
# leave the original alone, and vice versa.
require "set"

orig = Set["a", "b"]

copy = orig.dup
copy << "c"
p orig.to_a.sort                          # ["a", "b"]
p copy.to_a.sort                          # ["a", "b", "c"]

cloned = orig.clone
cloned.add("z")
cloned.delete("a")
p orig.to_a.sort                          # ["a", "b"]
p cloned.to_a.sort                        # ["b", "z"]

merged = orig.dup
merged.merge(["q", "b"])
p orig.to_a.sort                          # ["a", "b"]
p merged.to_a.sort                        # ["a", "b", "q"]

# writing to the original leaves an earlier copy alone too
orig << "w"
p copy.to_a.sort                          # ["a", "b", "c"]

p orig.equal?(copy)                       # false
p orig == orig.dup                        # true
p orig.dup.class                          # Set
