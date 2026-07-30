# The allocation report, with and without per-site attribution. Not a snapshot
# test: the site is an address, so the Makefile asserts the SHAPE of the lines
# (a type line without sites, a `site;type` line with them).
def make_array
  [1, 2, 3]
end

def make_hash
  { "k" => "v" }
end

4.times { make_array }
2.times { make_hash }
puts "done"
