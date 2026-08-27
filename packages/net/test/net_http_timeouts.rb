# open_timeout and read_timeout, honoured rather than stored. Before this they
# were accepted, assigned in #initialize and read nowhere, so a peer that
# accepted the connection and never answered held the caller forever whatever
# the timeout said. (matz/spinel#4133)
#
# No network: a listening socket that is never accept()ed still completes the
# handshake into the backlog, so the connect succeeds and nothing ever replies.
require "net/http"

server = TCPServer.new("127.0.0.1", 0)
port = server.addr[1]

http = Net::HTTP.new("127.0.0.1", port)
http.read_timeout = 1
http.start
begin
  http.request(Net::HTTP::Get.new("/"))
  puts "WRONG: returned"
rescue Net::ReadTimeout => e
  puts "read: #{e.class}"
end
http.finish
server.close

# The classes are CRuby's, and share CRuby's base, so `rescue Timeout::Error`
# catches either -- which is what a caller with a deadline writes.
p Net::ReadTimeout.new(nil).is_a?(Timeout::Error)
p Net::OpenTimeout.new(nil).is_a?(Timeout::Error)
p Net::ReadTimeout.new(nil).is_a?(RuntimeError)
p Net::ReadTimeout.ancestors.take(4).map(&:to_s)
p Net::OpenTimeout.ancestors.take(4).map(&:to_s)
p Net::ReadTimeout.new(nil).message
p Net::OpenTimeout.new(nil).message

begin
  raise Net::ReadTimeout
rescue Timeout::Error => e
  puts "rescued as Timeout::Error: #{e.class}"
end

# The defaults are CRuby's 60, and a timeout of 0 or less means no limit.
h2 = Net::HTTP.new("127.0.0.1", 1)
p h2.read_timeout
p h2.open_timeout

# A server that DOES answer is not affected by having a timeout set.
srv2 = TCPServer.new("127.0.0.1", 0)
p2 = srv2.addr[1]
t = Thread.new do
  c = srv2.accept
  while (l = c.gets)
    break if l.strip.empty?
  end
  c.write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi")
  c.close
  :served
end
ok = Net::HTTP.new("127.0.0.1", p2)
ok.read_timeout = 5
ok.start
res = ok.request(Net::HTTP::Get.new("/"))
p [res.code, res.body]
ok.finish
p t.value
srv2.close
