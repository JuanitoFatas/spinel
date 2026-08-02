# A `[]` read on a poly receiver emits a cls_id switch: an arm per user class
# that defines `[]`, then one per builtin ARRAY kind. A Hash arriving at the
# same read matched none of them, and with no default arm the result kept its
# nil initializer, so the read answered nil with nothing raised (#3507). The
# switch now ends in the runtime index, which dispatches on the receiver's own
# kind.
class PxBag
  def [](k)
    "bag"
  end
end

class PxRel
  def initialize(items)
    @items = items
  end

  def group
    out = {}
    @items.each do |x|
      k = yield x
      a = out.fetch(k, nil)
      if a.nil?
        a = []
        out[k] = a
      end
      a << x
    end
    out
  end
end

p PxBag.new[1]

rel = PxRel.new([1, 2, 3])
groups = rel.group { |x| x }
p groups.length
p groups[2]
p groups[1]
p groups[99]

# the same read with keys of another kind
strs = PxRel.new(["a", "bb"]).group { |s| s }
p strs["a"]
p strs["bb"]

# and the array kinds the switch already covered stay working
p [10, 20][1]
p ["a", "b"][0]
p [1.5, 2.5][1]
p [[1], [2]][0]
