# Spinel bundled `digest` — a native binding with NO carried C.
#
# The hash implementations are the runtime's vendored crypto (lib/sp_crypto.c,
# always in libspinel_rt.a because the string/net runtime uses it); this
# package only declares the Ruby surface. Subset: the class-method hexdigest
# and digest forms. The incremental Digest::SHA256.new/update object API is
# not modelled.
module Digest
  module SHA256
    native_lib "digest"
    # :cstring return: sp_crypto's static-buffer contract (the next call
    # clobbers the buffer), so codegen dups the result onto the GC heap.
    native_func :hexdigest, [:string], :cstring, "sp_crypto_sha256_hex"
    # :cbinstr return: raw digest bytes, whose length comes from
    # sp_ffi_bin_len rather than strlen (a binary digest contains NULs).
    native_func :digest, [:string], :cbinstr, "sp_crypto_sha256_bin"
  end
  module SHA1
    native_lib "digest"
    native_func :hexdigest, [:string], :cstring, "sp_crypto_sha1_hex"
    native_func :digest, [:string], :cbinstr, "sp_crypto_sha1_bin"
  end
end
