p((1..).min(3))
p((..5).max(2))
p(("a".."e").min(2))
p(("a".."e").max(2))
r001 = ((1.0..5.0).min(2) rescue $!.class); p r001
r002 = ((1.0..5.0).max(2) rescue $!.class); p r002
p((1..5.0).min(2))
p((1..5).min(2))
p((1..5).max(2))
