# Time#min through a poly slot is the MINUTE, not an enumerable minimum.
# A boxed Time has no user elements, so sp_poly_min fell to its default and
# answered nil -- silently, and only for `min`: every sibling accessor reaches
# its own arm in emit_poly_builtin_method, which `min` never gets to because
# the enumerable fast path claims the name first.
#
# Both symptoms are covered. Which one the old code produced depended on
# something unrelated to the call: with no user class defining `min` it
# answered nil; with one, the poly switch raised NoMethodError instead.
class Widget
  def initialize(n)
    @n = n
  end

  def min
    @n
  end
end

t = Time.at(1788019003).utc

# A mixed array boxes its members.
box = [t, "x"]
p box[0].min
p box[0].hour

# The name is owned by a user class too -- the other symptom, same root.
p [Widget.new(7), "x"][0].min

# Every container kind sp_poly_min already served still answers as before.
p [[3, 1, 2], "x"][0].min
p [["c", "a", "b"], "x"][0].min
p [[2.5, 1.5], "x"][0].min
p [{ 2 => :b, 1 => :a }, "x"][0].min

# A typed receiver was always correct; it is the control.
p t.min
