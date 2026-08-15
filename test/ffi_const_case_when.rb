# `ffi_const` names a VALUE, not a class, so a `when` arm compares against it
# rather than testing the scrutinee's class. Its declarations live in their own
# table rather than the constant one, so the class-or-value question has to ask
# both -- read as a class name the arm folds to a constant false and the branch
# is never taken, with nothing said at compile time or run time.
# (Spinel-native FFI DSL, not valid CRuby, so the .expected is authored here.)
module M
  ffi_const :A, 1
  ffi_const :B, 2
  RUBY_C = 1
end

def kind(t)
  case t
  when M::A then "A"
  when M::B then "B"
  else "other"
  end
end

p kind(1)
p kind(2)
p kind(3)

t = 2
case t
when M::A, M::B then puts "either"
else puts "neither"
end

case "s"
when String then puts "String class still matches"
end

case M::A
when Integer then puts "an ffi_const is an Integer"
end

v = M::B
case v
when 2 then puts "literal arm matches the value"
end
