r = ((1..).last(2) rescue $!.class)
p r
p((1..).first(2))
p((1..).min)
p((1..5).last(2))
p((1..5).last)
p((1...5).last(2))
