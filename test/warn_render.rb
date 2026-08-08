# warn renders each message like puts: an Array is one line per element
# (recursively), everything else its to_s, and a trailing newline is not
# doubled. Non-scalars used to write a blank line.
warn ["x", "y"]
warn "z"
warn 1, 2
warn []
warn nil
warn :s
warn({ a: 1 })
warn "t\n"
warn ["a", ["b", "c"]]
warn 1.5

