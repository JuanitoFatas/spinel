# Array methods on a receiver whose class is only known at run time: an
# element read out of a mixed container is boxed, and the call has to unbox
# it to the Array it holds and answer as the typed call would. Covers the
# in-place mutators writing their result back into a typed original (an
# Array of Integers, of Strings, of Floats), the block forms, the
# Enumerable names over a boxed Hash, and the NoMethodError a non-Array
# receiver raises.
rows = [[3, 1, 2], "x"]
a = rows[0]
p a.sort!
p a.rotate!
p a.uniq!
p a.reverse
p a.flatten!
p a.fill(7, 1)
p rows[0]

words = [["pear", "fig", "apple"], 5]
w = words[0]
p w.sort!
p w.select! { |s| s.length > 3 }
p w.reject! { |s| s.start_with?("a") }
p w.keep_if { |s| s.length > 1 }
p w.delete_if { |s| s == "zzz" }
p w.sort_by! { |s| -s.length }
p w.filter! { |s| s.include?("e") }
p words[0]

floats = [[2.5, 1.5, 3.5], :sym]
f = floats[0]
p f.sort!
p f.to_ary
p f.fill(0.0)
p floats[0]

nested = [[[1, 2], [3, 4]], nil]
n = nested[0]
p n.transpose
p n.grep(Array)
p n.minmax_by { |x| x[1] }

h = [{a: 1, b: 2}, 0][0]
p h.grep(Array)
p h.minmax_by { |k, v| -v }

begin
  h.sort!
rescue NoMethodError => e
  puts e.message
end
begin
  s = ["str", 1][0]
  s.transpose
rescue NoMethodError => e
  puts e.message
end
