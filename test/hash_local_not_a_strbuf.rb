# The shared-mutable-string pass promotes a local from a name-keyed mutator
# table, and `[]=`, `insert`, `slice!` and `setbyte` are Hash's and Array's as
# much as String's. So `data = parse_object(s); data[k] = v` read as a string
# mutation of the result, demanded the callee's returned `out = {}` into the
# shared set, and the Hash came back through a `const char *` return. A local
# written from a container literal is not a string, whatever was done to it
# (#4177).
module SchematizedJson
  def self.parse_object(serialized)
    out = {}
    i = 0
    s = serialized.to_s
    while i < s.length
      out[s[i, 1].to_s] = i.to_s
      i = i + 1
    end
    out
  end

  def self.read_raw(data, key)
    return "" if !data.key?(key)
    data[key].to_s
  end

  def self.render_object(data)
    names = data.keys.sort
    out = "{"
    i = 0
    while i < names.length
      out = out + "," if i > 0
      out = out + names[i].to_s + ":" + read_raw(data, names[i].to_s)
      i = i + 1
    end
    out + "}"
  end

  def self.write_raw(serialized, key, raw)
    data = parse_object(serialized)
    data[key] = raw
    render_object(data)
  end
end

p SchematizedJson.write_raw("ab", "c", "9")
p SchematizedJson.read_raw(SchematizedJson.parse_object("ab"), "a")

# an Array local mutated by a name String shares is an Array too
def build
  rows = []
  rows[0] = "x"
  rows[1] = "y"
  rows
end
p build

# and a real shared-mutable String still works: the pass's own shape
def tack
  s = "a".dup
  t = s
  t << "b"
  [s, t]
end
p tack
