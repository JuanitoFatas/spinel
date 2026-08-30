# A `prepend` in a class REOPEN compiled and did nothing: only the class's
# first body was scanned for prepends, and the reopen -- the very form the
# explicit-receiver diagnostic recommends -- was never looked at. Both
# bodies are scanned now, the same two passes `include` registration makes
# (#4200).
module Guard
  def hello
    "guarded " + super
  end
end

class A
  def hello = "hi"
  prepend Guard          # in the FIRST body
end

class B
  def hello = "hi"
end

class B
  prepend Guard          # in a REOPEN
end

puts "A: #{A.new.hello}"
puts "B: #{B.new.hello}"

# The authorization shape that found it: a guard that refuses some calls
# and calls super for the rest, prepended from a reopen.
module Authorized
  def subscribed(name)
    return "denied #{name}" if name == "secret"
    super
  end
end

class Channel
  def subscribed(name)
    "ok #{name}"
  end
end

class Channel
  prepend Authorized
end

c = Channel.new
puts c.subscribed("news")
puts c.subscribed("secret")

# Two prepends on one class, the second from a reopen: both wrappers
# stand. (Its calls sit after every reopen: the class graph is baked at
# compile time, so a call BETWEEN the reopens would see the final chain --
# docs/limitations.md territory, and not what this test pins.)
module Louder
  def cheer
    super.upcase
  end
end

module Politer
  def cheer
    super + ", please"
  end
end

class Megaphone
  def cheer = "go"
  prepend Louder
end

class Megaphone
  prepend Politer
end

puts Megaphone.new.cheer
