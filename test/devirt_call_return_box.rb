# A class-method call through `.class` devirtualizes when exactly one class in
# the receiver's hierarchy defines the name -- and then handed back whatever
# that one implementation returns, raw. Its sibling branch, for two or more
# implementations, unifies the returns and boxes each arm. So a call the
# inference typed poly (another class defines the name too, outside this
# hierarchy) assigned an sp_int into an sp_RbVal local (#4182).
class Other
  def self.insert(table, attrs)
    "a string"
  end
end

class Adapter
  def self.insert(table, attrs)
    attrs.length
  end
end

class Blob
  def initialize
    @a = Adapter.new
  end

  def save
    blob_id = @a.class.insert("blobs", { "a" => "1" })
    { "blob_id" => blob_id }
  end
end

p Blob.new.save["blob_id"]
p Other.insert("x", {})

# the same shape the other way round: the one implementation answers a String
# where the inference unified to poly
class AlsoOther
  def self.tag
    7
  end
end

class Tagger
  def self.tag
    "t"
  end
end

class Holder
  def initialize
    @t = Tagger.new
  end
  def read
    v = @t.class.tag
    { "v" => v }
  end
end
p Holder.new.read["v"]
p AlsoOther.tag
