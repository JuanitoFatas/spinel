m001 = "hello".match(/l+/)
p m001.frozen?
p m001.to_a.frozen?
p m001.string.frozen?
