# Spinel bundled `openssl` -- the SSL half only.
#
# The spelling is CRuby's, deliberately: the goal is that a program written
# against CRuby compiles here, so inventing a shorter name would be worth
# nothing. What is missing is missing the way a subset is missing things --
# OpenSSL::Digest, Cipher, PKey and most of X509 are not here, and a program
# that uses them fails at COMPILE time with an unresolved call rather than at
# run time somewhere deep.
#
# Spinel implements no TLS. sp_openssl.c is glue over the system libssl, and
# the trust anchors are the operating system's; nothing is bundled.
#
# SSLSocket is not an IO, exactly as in CRuby: it is an ordinary object that
# answers #to_io, which is why IO.select had to learn that protocol. The
# handle is an Integer naming a connection in the C table -- no SSL pointer
# is handed to a garbage-collected world.
require "openssl/buffering"

module OpenSSL
  module Native
    native_lib "openssl"
    native_obj "packages/openssl/sp_openssl.o"
    native_func :connect,      [:int, :string, :int], :int,    "sp_ssl_connect"
    native_func :read,         [:int, :int],          :string, "sp_ssl_read"
    native_func :write,        [:int, :string, :int], :int,    "sp_ssl_write"
    native_func :pending,      [:int],                :int,    "sp_ssl_pending"
    native_func :close,        [:int],                :int,    "sp_ssl_close"
    native_func :last_error,   [],                    :string, "sp_ssl_last_error"
    native_func :peer_subject, [:int],                :string, "sp_ssl_peer_subject"
    native_func :version,      [:int],                :string, "sp_ssl_version"
    native_func :cipher,       [:int],                :string, "sp_ssl_cipher"
    ffi_lib "ssl"
    ffi_lib "crypto"
  end

  module SSL
    VERIFY_NONE = 0
    VERIFY_PEER = 1

    class SSLError < StandardError
    end

    # Only the members an outbound HTTPS client reaches. set_params is the one
    # Net::HTTP calls; the rest of CRuby's forty-odd accessors are not here.
    class SSLContext
      attr_accessor :verify_mode
      attr_accessor :verify_hostname

      def initialize
        @verify_mode = VERIFY_PEER
        @verify_hostname = true
      end

      def set_params(params = nil)
        @verify_mode = VERIFY_PEER
        @verify_hostname = true
        self
      end
    end

    class SSLSocket
      # Not an IO: an ordinary object with the buffered surface mixed in and
      # the handle behind #to_io, exactly as CRuby has it.
      include OpenSSL::Buffering

      attr_accessor :hostname

      def initialize(io, context = nil)
        super()
        @io = io
        @context = context.nil? ? SSLContext.new : context
        @handle = -1
        @hostname = ""
      end

      def context
        @context
      end

      # CRuby's SSLSocket#to_io answers the underlying socket, and every
      # forwarder (fileno, addr, closed?) goes through it. IO.select uses it
      # to find the descriptor to wait on.
      def to_io
        @io
      end

      def connect
        h = Native.connect(@io.fileno, @hostname,
                           @context.verify_mode == VERIFY_NONE ? 0 : 1)
        if h < 0
          raise SSLError, "SSL_connect returned an error: #{Native.last_error}"
        end
        @handle = h
        self
      end

      def sysread(maxlen)
        raise SSLError, "not connected" if @handle < 0
        s = Native.read(@handle, maxlen)
        s.empty? ? nil : s
      end

      def syswrite(data)
        raise SSLError, "not connected" if @handle < 0
        n = Native.write(@handle, data, data.bytesize)
        raise SSLError, "SSL_write returned an error: #{Native.last_error}" if n < 0
        n
      end

      # Bytes already decrypted and waiting inside the record layer. An event
      # loop that waits on the descriptor alone will not see these: a whole
      # record can arrive in one read, leaving the fd quiet while the
      # application still has data to take. CRuby has the same trap and the
      # same escape hatch.
      def pending
        @handle < 0 ? 0 : Native.pending(@handle)
      end

      def sysclose
        return nil if @handle < 0
        Native.close(@handle)
        @handle = -1
        nil
      end

      def peer_subject
        @handle < 0 ? "" : Native.peer_subject(@handle)
      end

      def ssl_version
        @handle < 0 ? "" : Native.version(@handle)
      end

      def cipher_name
        @handle < 0 ? "" : Native.cipher(@handle)
      end
    end
  end
end
