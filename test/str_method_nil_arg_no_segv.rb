# #504. Several String methods receiving the wrong arg shape used
# to SEGV. CRuby raises ArgumentError; spinel originally could only
# avoid the crash (TypeError / silent no-op / FrozenError stood in),
# and the builtin positional-arity guard now raises CRuby's own
# ArgumentError for these counts, so the outputs below match CRuby.

# count / delete with no arg: CRuby's ArgumentError, message and all
begin
  +"foo".count
  puts "BUG count: no raise"
rescue ArgumentError => e
  puts "count: #{e.message}"
end
begin
  p +"foo".delete
  puts "BUG delete: no raise"
rescue ArgumentError => e
  puts "delete: #{e.message}"
end
p +"foo".rindex(/missing/)  # CRuby + spinel post-#532: nil. (was: -1)
p "abcdabcd".rindex(/c/)   # CRuby & spinel: 6 (new sp_re_rindex helper)
begin
  "foo".send(:<<)           # CRuby: ArgumentError. spinel: FrozenError -- the
                            # send desugar reaches the mutation emitter before
                            # the arity guard (after #886)
  puts "no raise"
rescue FrozenError, ArgumentError => e
  puts "send-lshift: " + e.message
end

# setbyte on a mutable copy of a literal: the write copies the static bytes
# and rebinds the variable (#2029). A bare literal (always frozen) or an
# explicit .freeze raises (pinned by bundle_classd_43).
(str = +"a")
begin
  str.setbyte(0, 98)
  puts "literal not frozen: " + str
rescue FrozenError => e
  puts "frozen literal: " + e.message
end
str2 = "a".dup
str2.setbyte(0, 98)
puts str2  # "b" (heap, mutates)
