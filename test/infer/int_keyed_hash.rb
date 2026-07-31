# ...and a slot with no array evidence at all still becomes a hash.
def build
  h = {}
  h[10] = 1
  h[500] = 2
  h
end
t = build
t[3] = 7
puts t[10] + t[500] + t[3]
puts t.size
