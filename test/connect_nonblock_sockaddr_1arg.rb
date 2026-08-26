# Socket#connect_nonblock 1-arg form: the packed sockaddr path. Mirrors
# the 2-arg shape: default raises IO::WaitWritable on EINPROGRESS;
# `exception: false` returns the :wait_writable symbol instead. A
# success path returns 0 (boxed as Integer 0); a refused connection
# raises immediately, regardless of the option, because refusal is
# not "in progress" but a hard ECONNREFUSED.
require "socket"

# Pack a 16-byte sockaddr_in for 127.0.0.1:1 (refused port). Addrinfo#
# to_sockaddr and Socket.sockaddr_in are not codegen-supported yet,
# so build the same layout the runtime reads: sa_family (LE) +
# sin_port (BE) + sin_addr + 8 zero bytes of padding.
def sockaddr_in(port, host)
  [Socket::AF_INET, port].pack("vn") +
    host.split(".").map(&:to_i).pack("C4") + ("\x00" * 8)
end

# 1) Default (raise IO::WaitWritable) on a refused port. The error
#    here is ECONNREFUSED, not WaitWritable - the kernel answers
#    synchronously because nothing is listening.
s1 = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
refused =
  begin
    s1.connect_nonblock(sockaddr_in(1, "127.0.0.1"))
  rescue => e
    e.class
  end
p refused
s1.close

# 2) Default raise IO::WaitWritable on a real handshake. Use a
#    server socket so the connect can complete in the background.
srv = TCPServer.new("127.0.0.1", 0)
port = srv.addr[1]
t = Thread.new { srv.accept }
s2 = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
default =
  begin
    s2.connect_nonblock(sockaddr_in(port, "127.0.0.1"))
    :connected
  rescue IO::WaitWritable
    :wait_writable
  end
p default
# second call after the handshake completes: Errno::EISCONN (server
# accepted so the kernel knows we're already connected).
begin
  s2.connect_nonblock(sockaddr_in(port, "127.0.0.1"))
  p :ok
rescue Errno::EISCONN
  p :eisconn
end
t.value.close rescue nil
srv.close
s2.close

# 3) `exception: false` returns the :wait_writable symbol instead
#    of raising. The poll loop above now needs no rescue.
srv2 = TCPServer.new("127.0.0.1", 0)
port2 = srv2.addr[1]
t2 = Thread.new { srv2.accept }
s3 = Socket.new(Socket::AF_INET, Socket::SOCK_STREAM, 0)
flag = s3.connect_nonblock(sockaddr_in(port2, "127.0.0.1"), exception: false)
p flag
IO.select(nil, [s3])
again = s3.connect_nonblock(sockaddr_in(port2, "127.0.0.1"), exception: false)
p again
t2.value.close rescue nil
srv2.close
s3.close
