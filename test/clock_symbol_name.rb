# `Process.clock_gettime` takes a clock as a SYMBOL name as well as a constant
# or a raw id. The symbol went into an Integer slot and raised (#4044).
p Process.clock_gettime(:CLOCK_MONOTONIC).class
p Process.clock_gettime(Process::CLOCK_MONOTONIC).class
p Process.clock_gettime(:CLOCK_REALTIME).class
p Process.clock_gettime(:CLOCK_MONOTONIC, :nanosecond).class
p Process.clock_gettime(:CLOCK_MONOTONIC, :millisecond).class
p Process.clock_getres(:CLOCK_MONOTONIC).class
p Process.clock_gettime(:CLOCK_REALTIME) > 0
p Process.clock_gettime(:CLOCK_PROCESS_CPUTIME_ID) >= 0
