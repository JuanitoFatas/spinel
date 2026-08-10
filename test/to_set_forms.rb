require 'set'
p([1, 2].to_set.to_set.to_a)
s = [1, 2].to_set
p s.to_set.to_a
p [3, 4].to_set { |x| x * 2 }.to_a rescue p $!.class
t = Set[1,2]
p t.to_set.equal?(t)
