# `for x in <boxed value>` fell through to a comment and ran zero times,
# silently: a method whose return the inference widened to poly -- two return
# paths, a built hash and an empty literal -- made every `for` over its result
# a no-op, and the caller saw an empty result with nothing to point at. The
# runtime materializer answers what each kind iterates, and a value that
# iterates nothing raises NoMethodError as CRuby does (#4184).
def poly(n)
  case n
  when 0 then { "a" => 1, "b" => 2 }
  when 1 then [10, 20]
  when 2 then (1..3)
  when 3 then {}
  else 7
  end
end

out = []
for pair in poly(0)
  out << pair
end
p out

out2 = []
for k, v in poly(0)
  out2 << [k, v]
end
p out2

out3 = []
for x in poly(1)
  out3 << x
end
p out3

out4 = []
for x in poly(2)
  out4 << x
end
p out4

out5 = []
for x in poly(3)
  out5 << x
end
p out5

begin
  for x in poly(4)
    p x
  end
rescue NoMethodError
  puts "NoMethodError"
end

# the reported shape: a rescue gives the method two return paths
module Bug
  def self.parse_lines(lines)
    config = {}
    for line in lines
      config[line.strip] = line.strip
    end
    config
  end

  def self.parse
    parse_lines("a = 1\nb = 2\n".split("\n"))
  rescue StandardError
    {}
  end

  def self.load
    sections = parse
    models = {}
    for pair in sections
      models[pair[0]] = pair[1]
    end
    models
  end
end
p Bug.load.length
p Bug.load.keys.sort
