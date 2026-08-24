# A method local captured by a block is reached through a heap cell. When the
# method is INLINED at its call site -- which a `yield` in it forces -- the
# inlined body kept the cell form while the prologue declared a plain renamed
# local, and nothing declared the cell (#4088). Two halves: the cell form did
# not go through the rename map that the plain form did, and the three inline
# emitters each declared the plain form for a celled local.
#
# Same symptom as #4087 from the other side: there the block became a function
# of its own, here the callee is folded into the caller.

module M
  def self.sum_of(list)
    n = 0
    list.each { |x| n += yield x }
    n
  end
end

def run(list) = M.sum_of(list) { |x| x }
puts run([1, 2])
puts run([3, 4, 5])

# the cell types that are not sp_int
module F
  def self.join_of(list)
    s = ""
    list.each { |x| s += (yield x).to_s }
    s
  end

  def self.total_of(list)
    f = 0.0
    list.each { |x| f += (yield x) }
    f
  end

  def self.gather(list)
    a = []
    list.each { |x| a.push(yield x) }
    a
  end
end

def joined(list) = F.join_of(list) { |x| x }
def totalled(list) = F.total_of(list) { |x| x * 1.0 }
def gathered(list) = F.gather(list) { |x| x }
p joined([1, 2])
p totalled([1, 2])
p gathered([1, 2])

# a closure over the celled local outlives the loop
module K
  def self.keep(list)
    seen = []
    list.each { |x| seen.push(yield x) }
    seen.length
  end
end
def kept(list) = K.keep(list) { |x| x }
p kept([1, 2, 3])
