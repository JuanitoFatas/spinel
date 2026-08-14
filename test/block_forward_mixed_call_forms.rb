def fwd(&b)
  [1, 2].map(&b)
end
tri = ->(x) { x * 3 }
p fwd(&tri)
p(fwd { |x| x * 3 })

def fwd2(&b)
  [1, 2].map(&b)
end
p(fwd2 { |x| x * 3 })
tri2 = ->(x) { x * 3 }
p fwd2(&tri2)

def fwd3(&b)
  [1, 2].map(&b)
end
str = ->(x) { "s#{x}" }
p fwd3(&str)
p(fwd3 { |x| x * 3 })
p(fwd3 { |x| x.to_s })

def sel(&b)
  [1, 2].select(&b)
end
odd = ->(x) { x.odd? }
p sel(&odd)
p(sel { |x| x > 1 })

def ea(&b)
  [1, 2].each(&b)
end
pr = ->(x) { p x }
ea(&pr)
ea { |x| p x * 10 }
