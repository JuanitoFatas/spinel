# Issue #723. `.encoding` returns the source label as a small Encoding value.
# spinel is UTF-8 throughout except for the ASCII-8BIT tag a string can carry
# (pack's answer, String#b); this used to fold to the constant UTF-8 while
# discarding the receiver, so the expected output recorded the wrong answer
# for `.b` rather than CRuby's.

puts "hello".encoding
puts "x".encode.encoding
puts "y".b.encoding
puts "hello".encoding.class
puts "hello".encoding.name
