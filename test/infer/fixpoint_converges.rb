# The inference fixpoint has a 128-round cap. Reaching it is not a slow path,
# it is a non-answer: the loop stops because the cap says so, mid-oscillation,
# and where it stops decides which of two typings gets emitted. Both typings
# usually pass the test -- a boxed slot prints the same answer as a typed one --
# so nothing else in the suite can see it. (#4116)
#
# `require "pathname"` alone used to reach the cap, twice, and cost a 53k-line
# tree 124.6s in the front end against 11.5s converged.
require "pathname"

# A few shapes that were capped for their own reasons: IO.pipe's two targets,
# a case/in binding, and a File.open block parameter.
r, w = IO.pipe
w.write("x")
w.close
puts r.read

case [1, "a"]
in [Integer => n, String => s]
  puts "#{n}#{s}"
end

p Pathname.new(".").directory?
