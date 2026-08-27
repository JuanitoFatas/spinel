# Thread.list returns the main thread plus every live spawned thread.
#
# The count after spawning is only meaningful if the spawned threads cannot
# have finished yet, so each one blocks on a queue the main thread controls.
# This used to lean on "at N=1 a spawned thread does not run until the current
# thread yields" instead, which stopped being true the moment the test was
# linked against the runtime the driver actually ships.
q = Queue.new

p Thread.list.size                       # 1 (just main)
p Thread.list.include?(Thread.current)   # true

threads = (1..3).map { Thread.new { q.pop } }
p Thread.list.size                       # 4 (main + 3 parked on the queue)

3.times { q.push(1) }
threads.each(&:join)
p Thread.list.size                       # 1 (the spawned threads finished)
p Thread.list.include?(Thread.main)      # true
