# The allocation report, with and without per-site attribution. Not a snapshot
# test: the site is an address, so the Makefile asserts the SHAPE of the lines
# (a type line without sites, a `site;type` line with them).
def make_array
  [1, 2, 3]
end

def make_hash
  { "k" => "v" }
end

# strings carry no GC scan callback, so they take a reserved key of their own
# in the same table -- the report must name a site for them like any other type
def make_string(i)
  "item-#{i}"
end

4.times { make_array }
2.times { make_hash }
3.times { |i| make_string(i) }
puts "done"
