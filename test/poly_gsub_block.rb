# A class defining a nil-returning `#to_s` anywhere in the program used to
# make `gsub`/`sub` WITH A BLOCK on a widened (poly) receiver infer as
# returning void: the poly-dispatch inference had a rule for the two-argument
# blockless form but none for the block form, so the call fell through
# unresolved -- either a bad C build, or (when the value reached a String
# consumer instead) a runtime NoMethodError claiming a String had no #gsub.

class Poison
  def to_s
    @never_set
  end
end

def escape(text)
  text.to_s.gsub(/[&<>]/) { |c| c == "<" ? "&lt;" : "&gt;" }
end

def first_vowel(text)
  text.to_s.sub(/[aeiou]/) { |c| c.upcase }
end

puts escape("a<b")        # a&lt;b
puts escape(123)          # 123
puts first_vowel("hello") # hEllo
puts first_vowel(42)      # 42

# The blockless, two-argument form already worked before this fix; keep it
# exercised here so a future change can't silently narrow the new rule.
h = { stream: "abc/def", count: 5 }
puts h[:stream].gsub(/[^a-z]/, "_") # abc_def
