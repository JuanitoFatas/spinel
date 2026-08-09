h001 = {}
h001.default_proc = proc { |hh001, k001| k001.to_s }
p h001[:x]
h002 = {}
h002.default_proc = Proc.new { |hh002, k002| k002.to_s }
p h002[:x]
l003 = lambda { |hh003, k003| k003.to_s }
h003 = {}
h003.default_proc = l003
p h003[:y]
h004 = {}
h004.default_proc = ->(hh, k) { "L#{k}" }
p h004[:z]
