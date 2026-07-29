# Spinel bundled `set`.
#
# A Set backed by an Array, preserving insertion order. Elements are kept
# unique by #eql? (with #hash) like CRuby's Hash-backed Set -- a value object
# that defines #eql?/#hash is deduplicated and found by #include? even across
# distinct instances, and 1 and 1.0 are distinct members (1.eql?(1.0) is
# false). This covers the standard Set surface (construction, iteration,
# membership, set algebra, comparison, classify/divide/flatten) rather than
# every CRuby corner.

class Set
  include Enumerable

  # Set[1, 2, 3] builds a Set from the given elements (CRuby's Set.[]).
  def self.[](*args)
    new(args)
  end

  def initialize(enum = nil)
    @data = []
    if enum
      Set.check_enum(enum)
      if block_given?
        enum.each { |x| add(yield(x)) }
      else
        enum.each { |x| add(x) }
      end
    end
  end

  # dup and clone must not share the element array with the original.
  def initialize_copy(orig)
    @data = orig.to_a
  end

  def add(x)
    @data.push(x) unless include?(x)
    self
  end

  def <<(x)
    add(x)
  end

  # add? returns nil when the element was already present.
  def add?(x)
    return nil if include?(x)
    @data.push(x)
    self
  end

  def delete(x)
    @data.delete_if { |e| e.eql?(x) }
    self
  end

  # delete? returns nil when the element was absent.
  def delete?(x)
    return nil unless include?(x)
    @data.delete_if { |e| e.eql?(x) }
    self
  end

  def include?(x)
    @data.any? { |e| e.eql?(x) }
  end
  alias member? include?
  alias === include?

  def each
    _i = 0
    while _i < @data.size
      x = @data[_i]
      yield x
      _i += 1
    end
    self
  end

  def size
    @data.size
  end
  alias length size

  def empty?
    @data.empty?
  end

  def clear
    @data = []
    self
  end

  def to_a
    @data.dup
  end

  def map
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      r.push(yield(x))
      _i += 1
    end
    r
  end
  alias collect map

  # In-place block methods. map!/collect! replace the elements (re-deduplicating);
  # keep_if/delete_if always return self; select!/reject! return self when
  # anything changed and nil otherwise (CRuby's Set contract).
  def map!
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      v = yield(x); r.push(v) unless r.any? { |e| e.eql?(v) }
      _i += 1
    end
    @data = r
    self
  end
  alias collect! map!

  def keep_if
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      r.push(x) if yield(x)
      _i += 1
    end
    @data = r
    self
  end

  def delete_if
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      r.push(x) unless yield(x)
      _i += 1
    end
    @data = r
    self
  end

  def select!
    n = @data.size
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      r.push(x) if yield(x)
      _i += 1
    end
    @data = r
    @data.size == n ? nil : self
  end
  alias filter! select!

  def reject!
    n = @data.size
    r = []
    _i = 0
    while _i < @data.size
      x = @data[_i]
      r.push(x) unless yield(x)
      _i += 1
    end
    @data = r
    @data.size == n ? nil : self
  end

  def merge(enum)
    Set.check_enum(enum)
    enum.each { |x| add(x) }
    self
  end

  def replace(enum)
    Set.check_enum(enum)
    @data = []
    enum.each { |x| add(x) }
    self
  end

  # CRuby rebuilds the hash index here; the array-backed set has no index,
  # so reset only has to return self.
  def reset
    self
  end

  def subtract(enum)
    Set.check_enum(enum)
    enum.each { |x| @data.delete_if { |e| e.eql?(x) } }
    self
  end

  # The combining ops reject a non-enumerable operand with ArgumentError rather
  # than letting the #each/#include? call fail as NoMethodError, matching CRuby.
  def self.check_enum(x)
    raise ArgumentError, "value must be enumerable" unless x.respond_to?(:each)
    x
  end

  # Set operators build fresh sets; the operand only needs #include? /#each.
  def &(other)
    Set.check_enum(other)
    r = Set.new
    @data.each { |x| r.add(x) if other.include?(x) }
    r
  end
  alias intersection &

  def |(other)
    Set.check_enum(other)
    r = Set.new(@data)
    other.each { |x| r.add(x) }
    r
  end
  alias union |
  alias + |

  def -(other)
    Set.check_enum(other)
    r = Set.new
    @data.each { |x| r.add(x) unless other.include?(x) }
    r
  end
  alias difference -

  def ^(other)
    (self | other) - (self & other)
  end

  def ==(other)
    return false unless other.is_a?(Set)
    size == other.size && subset?(other)
  end
  alias eql? ==

  # Content-based, order-independent: equal sets hash equal (#3069).
  # XOR of the element hashes is commutative, so insertion order is moot.
  def hash
    h = size
    @data.each { |x| h ^= x.hash }
    h
  end

  def subset?(other)
    @data.all? { |x| other.include?(x) }
  end
  alias <= subset?

  def superset?(other)
    other.subset?(self)
  end
  alias >= superset?

  def proper_subset?(other)
    size < other.size && subset?(other)
  end
  alias < proper_subset?

  def proper_superset?(other)
    size > other.size && superset?(other)
  end
  alias > proper_superset?

  def <=>(other)
    return nil unless other.is_a?(Set)
    return 0 if size == other.size && subset?(other)
    return -1 if subset?(other)
    return 1 if superset?(other)
    nil
  end

  def disjoint?(other)
    @data.each { |x| return false if other.include?(x) }
    true
  end

  def intersect?(other)
    !disjoint?(other)
  end

  # classify { |x| key } -> { key => Set of members }
  def classify
    # The nil pre-write pins k to a boxed (poly) key; see divide.
    h = {}
    @data.each do |x|
      k = nil
      k = yield(x)
      h[k] = Set.new unless h.key?(k)
      h[k].add(x)
    end
    h
  end

  # divide { |x| key } -> Set of member Sets grouped by the block value.
  # divide { |x, y| rel } -> Set of member Sets, each a strongly connected
  # component of the relation graph (mutual reachability, matching CRuby's
  # tsort-based form; a one-way relation does not merge its endpoints).
  def divide(&func)
    if func.arity == 2
      # 2-arity: strongly connected components of the relation graph (mutual
      # reachability, matching CRuby's tsort-based form; a one-way relation
      # does not merge its endpoints). reach is a flat n*n boolean closure.
      a = @data.dup
      n = a.size
      reach = []
      n.times do |i|
        n.times do |j|
          rel = i == j
          rel = true if func.call(a[i], a[j])
          reach.push(rel)
        end
      end
      n.times do |k|
        n.times do |i|
          n.times do |j|
            reach[i * n + j] = true if reach[i * n + k] && reach[k * n + j]
          end
        end
      end
      rep = []
      n.times do |i|
        ri = i
        j = 0
        while j < i
          if reach[i * n + j] && reach[j * n + i]
            ri = rep[j]
            break
          end
          j += 1
        end
        rep.push(ri)
      end
      groups = {}
      n.times do |i|
        k = rep[i]
        groups[k] = Set.new unless groups.key?(k)
        groups[k].add(a[i])
      end
      res = Set.new
      groups.each_value { |s| res.add(s) }
      res
    else
      # 1-arity: group by the block value. The nil pre-write pins k to a
      # boxed (poly) key: call sites with different block value types share
      # this body, and a concretely-typed k would collapse them to one type.
      h = {}
      @data.each do |x|
        k = nil
        k = func.call(x)
        h[k] = Set.new unless h.key?(k)
        h[k].add(x)
      end
      r = Set.new
      h.each_value { |s| r.add(s) }
      r
    end
  end

  # flatten recursively merges nested Set elements into a flat Set.
  def flatten
    r = Set.new
    @data.each do |x|
      if x.is_a?(Set)
        x.flatten.each { |y| r.add(y) }
      else
        r.add(x)
      end
    end
    r
  end

  # flatten!: in-place flatten; returns self when any nested Set was merged,
  # nil when the set was already flat (CRuby's contract). Rebuilds @data
  # through add rather than assigning flatten.to_a into it -- that write
  # would make @data's inferred type depend on itself (to_a dups @data)
  # and widen the ivar to poly.
  def flatten!
    nested = false
    @data.each { |x| nested = true if x.is_a?(Set) }
    return nil unless nested
    f = flatten
    @data = []
    f.each { |y| add(y) }
    self
  end

  def inspect
    "Set[" + @data.map { |x| x.inspect }.join(", ") + "]"
  end
  alias to_s inspect
end
