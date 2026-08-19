# Integer#coerce / Float#coerce with a non-numeric argument. CRuby answers
# `[Float(other), Float(self)]`, so the errors are Float()'s: a TypeError for
# nil / true / an Array / a Symbol, and an ArgumentError for an unparseable
# String. spinel put the argument straight into the Integer pair's slot, so a
# String stopped the C BUILD and a nil answered a coerced 0 (#4011).
[nil, true, false, "x", [1], { a: 1 }, :s].each do |v|
  begin
    p 5.coerce(v)
  rescue => e
    puts "#{v.inspect} => #{e.class}: #{e.message}"
  end
  begin
    p 1.5.coerce(v)
  rescue => e
    puts "1.5 #{v.inspect} => #{e.class}: #{e.message}"
  end
end

# the same, written as literals at the call
begin; p 5.coerce("x"); rescue => e; puts "#{e.class}: #{e.message}"; end
begin; p 5.coerce([1]); rescue => e; puts "#{e.class}: #{e.message}"; end
begin; p 5.coerce(nil); rescue => e; puts "#{e.class}: #{e.message}"; end
begin; p 1.5.coerce(nil); rescue => e; puts "#{e.class}: #{e.message}"; end

# a parseable String is still a number
p 5.coerce("2.5")

# and the numeric pairs are unchanged
p 5.coerce(2)
p 5.coerce(2.5)
p 5.coerce(2**70)
p 1.5.coerce(3)
v = [1, 2][1]
p 5.coerce(v)
