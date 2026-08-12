# An ffi type list may be written the way a real adapter writes a long one:
# a constant, or `[...] * n`. Both are compile-time constants, so they name
# the same function as the literal (#3804).
module Demo
  module Ext
    TYPES = [:float].freeze
    ffi_func :fabsf, TYPES, :float
    ffi_func :fmaxf, [:float] * 2, :float
    ffi_func :fminf, [:float, :float], :float
  end
end

puts Demo::Ext.fabsf(-2.5)
puts Demo::Ext.fmaxf(1.5, 2.5)
puts Demo::Ext.fminf(1.5, 2.5)
