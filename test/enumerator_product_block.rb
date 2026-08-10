Enumerator.product([1, 2], [3]) { |a, b| p [a, b] }
r = Enumerator.product([1, 2], [3]) { |a, b| }
p r
p Enumerator.product([1, 2], [3]).to_a
