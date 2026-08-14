#include <stddef.h>

#include "ffi_spec.h"

/* The single source of truth for the FFI type-spec vocabulary. Each row pairs a
 * spec token with its inferred Ruby type and its C type at the ABI boundary;
 * ffi_spec_to_ty and ffi_c_type are thin lookups into it. LP64 target. */
static const FfiSpecInfo FFI_SPECS[] = {
  { "int",         TY_INT,         "int"             },
  { "uint32",      TY_INT,         "uint32_t"        },
  { "int32",       TY_INT,         "int32_t"         },
  { "uint16",      TY_INT,         "uint16_t"        },
  { "int16",       TY_INT,         "int16_t"         },
  { "uint8",       TY_INT,         "uint8_t"         },
  { "int8",        TY_INT,         "int8_t"          },
  { "size_t",      TY_INT,         "size_t"          },
  { "long",        TY_INT,         "long"            },
  { "int64",       TY_INT,         "int64_t"         },
  { "float",       TY_FLOAT,       "float"           },
  { "double",      TY_FLOAT,       "double"          },
  { "bool",        TY_BOOL,        "int"             },
  { "str",         TY_STRING,      "const char *"    },
  { "binstr",      TY_STRING,      "const char *"    },  /* bytes + sp_ffi_bin_len */
  { "ptr",         TY_POLY,        "void *"          },
  { "float_array", TY_FLOAT_ARRAY, "const double *"  },
  { "int_array",   TY_INT_ARRAY,   "const int64_t *" },
  { "void",        TY_NIL,         "void"            },
  /* ffi-gem (CRuby `ffi`) spellings, accepted as aliases so the gem's
     `attach_function` declarations compile unchanged. LP64 target. */
  { "string",      TY_STRING,      "const char *"    },
  { "pointer",     TY_POLY,        "void *"          },
  { "buffer_in",   TY_POLY,        "void *"          },
  { "buffer_out",  TY_POLY,        "void *"          },
  { "buffer_inout",TY_POLY,        "void *"          },
  { "char",        TY_INT,         "int8_t"          },
  { "uchar",       TY_INT,         "uint8_t"         },
  { "short",       TY_INT,         "int16_t"         },
  { "ushort",      TY_INT,         "uint16_t"        },
  { "uint",        TY_INT,         "uint32_t"        },
  { "ulong",       TY_INT,         "unsigned long"   },
  { "long_long",   TY_INT,         "int64_t"         },
  { "ulong_long",  TY_INT,         "uint64_t"        },
  { "uint64",      TY_INT,         "uint64_t"        },
};

/* The `ffi_read_*` / `ffi_write_*` suffixes. A separate vocabulary from the
   spec tokens above (`u8` here, `uint8` there) because it is the surface
   syntax of a different declaration, but the same rule holds: one table, so
   the width a suffix promises is the width that is loaded and stored. */
static const struct { const char *kind; const char *c_type; } FFI_SCALAR_KINDS[] = {
  { "u8",  "uint8_t"  }, { "i8",  "int8_t"  },
  { "u16", "uint16_t" }, { "i16", "int16_t" },
  { "u32", "uint32_t" }, { "i32", "int32_t" },
  { "u64", "uint64_t" }, { "i64", "int64_t" },
};

const char *ffi_scalar_ctype(const char *kind) {
  if (!kind) return NULL;
  for (unsigned i = 0; i < sizeof(FFI_SCALAR_KINDS) / sizeof(FFI_SCALAR_KINDS[0]); i++)
    if (sp_streq(FFI_SCALAR_KINDS[i].kind, kind)) return FFI_SCALAR_KINDS[i].c_type;
  return NULL;
}

const FfiSpecInfo *ffi_spec_lookup(const char *spec) {
  if (!spec) return NULL;
  for (unsigned i = 0; i < sizeof(FFI_SPECS) / sizeof(FFI_SPECS[0]); i++)
    if (sp_streq(FFI_SPECS[i].spec, spec)) return &FFI_SPECS[i];
  return NULL;
}
