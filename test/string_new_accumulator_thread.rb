# The accumulator idiom under spinel's frozen-by-default literals: a `""`
# literal is frozen (docs/limitations.md), so the accumulator is built with
# String.new -- and the shared handle takes appends from inside a Thread
# block, which is the run_command shape in tools/spin.rb that #4207 found
# broken. (The `text = ""; text << x` spelling itself now gets a
# compile-time warning naming the escape hatches.)
text = String.new
t = Thread.new do
  3.times { |i| text << "line#{i}\n" }
end
t.join
print text
p text.length

# The +"" spelling of the same escape hatch.
acc = +""
["a", "b", "c"].each { |s| acc << s }
p acc

# And interpolation-born strings are mutable too.
n = 7
buf = "n=#{n}"
buf << "!"
p buf
