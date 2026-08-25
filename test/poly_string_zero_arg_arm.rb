# A String reaching the dispatch through a poly slot -- an element of a nested
# array, where inference does not narrow past the first level -- answered
# NoMethodError for methods String has. `strip` had an arm and its one-sided
# siblings did not, which is the shape of most of these.
#
# `to_str` is the one that matters beyond convenience: it is the implicit
# conversion protocol, so a poly slot holding a String has to answer it.
nested = [["k", "  Vv\tz  "]]
s = nested[0][1]
p s.lstrip
p s.rstrip
p s.to_str
p s.ascii_only?
p s.valid_encoding?
p s.scrub
p s.encode

# above ASCII, where ascii_only? has something to say
u = [["k", "日本"]]
p u[0][1].ascii_only?
p u[0][1].to_str
p u[0][1].lstrip
p u[0][1].valid_encoding?

# the whole point is that these agree with the same call on a plain String
plain = "  Vv\tz  "
p [s.lstrip == plain.lstrip, s.rstrip == plain.rstrip, s.to_str == plain.to_str]
p [s.ascii_only? == plain.ascii_only?, s.valid_encoding? == plain.valid_encoding?]

# a poly slot holding something that is not a String still raises
mixed = [1, "s"]
def at(a, i); a[i]; end
begin
  at(mixed, 0).lstrip
rescue NoMethodError => e
  puts "NoMethodError"
end
