# The zero-arg enumerable helpers (min max first last sum) on a boxed
# receiver: a Range takes the helper's own arm for all five names -- also
# when a user class owns the name, which used to steer every non-container
# into the user switch's NoMethodError -- and a receiver that is not a
# container raises NoMethodError as CRuby does, where min/max answered a
# silent nil (#4192 follow-up).
class Owner
  def min; 100; end
  def max; 200; end
  def first; 300; end
  def last; 400; end
  def sum; 500; end
end

r = [(1..5), "x"]
p r[0].min
p r[0].max
p r[0].first
p r[0].last
p r[0].sum

e = [(1...5), "x"]
p e[0].max
p e[0].last

o = [Owner.new, "x"]
p o[0].min
p o[0].sum

t = [Time.at(1788019003).utc, "x"]
p t[0].min

n = [nil, 7]
begin
  p n[0].min
rescue NoMethodError
  puts "min raises for nil"
end
begin
  p n[1].max
rescue NoMethodError
  puts "max raises for Integer"
end
