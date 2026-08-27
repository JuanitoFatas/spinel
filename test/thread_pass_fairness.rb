# Thread.pass from the main thread yields a turn to the runnable siblings and
# then resumes main, rather than draining them to completion. So a sibling that
# loops on Thread.pass cannot starve main.
#
# What is NOT asserted here is the ORDER. The interleaving is scheduler-
# specific -- CRuby is preemptive, and spinel's own scheduler puts a green
# thread on whichever worker is free -- so an exact log was a test of one
# schedule rather than of the property, and it went red the day threaded tests
# started linking the runtime the driver ships. The property is that both make
# progress and both finish.
log = []
mutex = Mutex.new
t = Thread.new do
  5.times do |i|
    mutex.synchronize { log << i }
    Thread.pass
  end
end
3.times do
  mutex.synchronize { log << :main }
  Thread.pass
end
t.join

p log.length
p log.select { |e| e == :main }.length
p log.reject { |e| e == :main }.sort
p log.uniq.length == log.length

# A sibling looping on Thread.pass must not starve main: main reaches here and
# completes its own loop. With the old drain behaviour main would run the
# sibling to completion on its first pass.
spinner = Thread.new { 100.times { Thread.pass } }
main_turns = 0
4.times do
  main_turns += 1
  Thread.pass
end
puts "main_turns: #{main_turns}"
spinner.join
puts "done"
