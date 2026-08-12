require "json"

class Point
  def initialize(x, y)
    @x = x
    @y = y
  end

  def to_json(*args)
    { x: @x, y: @y }.to_json(*args)
  end
end

p Point.new(1, 2).to_json
p JSON.generate({ points: [Point.new(1, 2), Point.new(3, 4)], n: 2 })
puts JSON.pretty_generate([Point.new(5, 6)])
