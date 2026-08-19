#!/usr/bin/env ruby

# Generate the positional-arity spec tables in src/codegen_call.c
# (sp_builtin_arity_spec_tbl and sp_builtin_cmeth_arity_spec_tbl) by probing
# the local CRuby:
#
#   ruby tools/gen_builtin_arity_spec.rb            # print both tables
#   ruby tools/gen_builtin_arity_spec.rb --write    # splice them into
#                                                   # src/codegen_call.c
#
# Probing technique, per (class, method):
#
#   1. Counts 0..3 are called with nil arguments to locate the true MINIMUM.
#      An outcome other than a "wrong number of arguments" ArgumentError -- a
#      TypeError, a RangeError, success -- proves the COUNT is accepted (the
#      values were the problem). This must run on NON-EMPTY receivers: several
#      C methods take an empty-receiver fast path that returns before the
#      arity check ("".delete never raises), which would hide the method.
#   2. A 99-argument call makes CRuby name the accepted range in its own
#      message ("given 99, expected 0..1"), yielding the MAXIMUM and CRuby's
#      exact wording. The low-side message is captured separately from a
#      (min-1)-argument call, because branch-implemented methods word the two
#      sides differently (Range#first: 0 args is a bare read, 1.. args is
#      "expected 1").
#   3. Every count 0..12 is then VERIFIED against the derived min/max. A
#      method whose accepted set is not one contiguous range (Time.utc takes
#      1..8 OR exactly 10) is left out -- unguarded -- rather than guessed.
#
# The instance surface probed is exactly sp_builtin_arity_tbl (the Method#arity
# dump already in codegen_call.c); the class-method surface is the curated
# list below. Anything the probe cannot prove is omitted, so the guard the
# tables drive fires only where CRuby itself would raise.

require "timeout"

Warning[:deprecated] = false  # probing deprecated arg shapes is the point

ROOT = File.expand_path("..", __dir__)
SOURCE = File.join(ROOT, "src/codegen_call.c")

# Non-empty receivers (see note above).
INSTANCE_RECEIVERS = {
  "String" => "ab", "Integer" => 1, "Float" => 1.0, "Symbol" => :a,
  "Array" => [1, 2], "Hash" => {1 => 2}, "Range" => (1..2), "Time" => Time.at(0),
}

# Class/module methods: the constructors and module functions whose emitters
# index argv[] unconditionally (File.open with no arguments crashed the
# compiler). Never probe anything that could act on the probing process
# itself (fork / exec / select / sleep).
CLASS_TARGETS = {
  "File"    => %w[open new read write binread binwrite readlines foreach delete unlink
                  rename exist? size basename dirname extname join split expand_path
                  chmod utime umask truncate symlink link readlink realpath stat lstat
                  ftype mtime atime ctime empty? zero? identical? absolute_path],
  "IO"      => %w[for_fd sysopen new open read write binread binwrite readlines pipe],
  "Dir"     => %w[new open mkdir rmdir delete unlink entries children glob foreach
                  exist? empty? home pwd getwd chdir],
  "Time"    => %w[at local mktime utc gm now],
  "Hash"    => %w[new],
  "Array"   => %w[new],
  "String"  => %w[new],
  "Integer" => %w[sqrt],
  "Math"    => %w[sqrt cbrt sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh
                  atanh log log2 log10 exp hypot ldexp frexp erf erfc gamma lgamma],
  "Process" => %w[getpriority setpriority getsid kill clock_gettime clock_getres pid ppid
                  uid gid euid egid setproctitle],
  "Regexp"  => %w[new escape quote union],
  "Random"  => %w[new rand srand],
  "ENV"     => %w[fetch key? has_key? include? member? assoc rassoc values_at],
  "Kernel"  => %w[format sprintf Integer Float String Array Hash Rational Complex],
  "Marshal" => %w[dump load],
}

def probe(recv, m, n)
  r2 = (recv.dup rescue recv)
  Timeout.timeout(2) { r2.__send__(m, *Array.new(n)) }
  :ok
rescue ArgumentError => e
  e.message[/wrong number of arguments \(given #{n}, expected ([^)]+)\)/, 1] || :ok
rescue Exception
  :ok  # the count was accepted; the values (or environment) were not
end

def spec_for(recv, m)
  sym = m.to_sym
  return nil unless recv.respond_to?(sym)
  low = (0..3).map { |n| probe(recv, sym, n) }
  min = low.index(:ok)
  return nil if min.nil?  # needs > 3 required args: not on this surface
  hi = probe(recv, sym, 99)
  max, hi_exp =
    case hi
    when :ok then [-1, nil]
    when /\A\d+\z/, /\A\d+\.\.\d+\z/ then [hi[/(\d+)\z/, 1].to_i, hi]
    when /\A\d+\+\z/ then [-1, nil]
    else return nil  # keyword-argument wording: leave the method unguarded
    end
  return nil if min == 0 && max == -1  # nothing to enforce
  lo_exp = min > 0 && low[min - 1].is_a?(String) ? low[min - 1] : nil
  # a minimum the probe proved but whose message we could not parse: skip the
  # low side rather than guess a message
  min = 0 if min > 0 && lo_exp.nil?
  return nil if min == 0 && max == -1
  # the spec must predict every count: a gapped acceptance set is left out
  (0..12).each do |n|
    predicted_ok = n >= min && (max < 0 || n <= max)
    actual = n <= 3 ? low[n] : probe(recv, sym, n)
    return nil if (actual == :ok) != predicted_ok
  end
  [min, max, lo_exp, hi_exp]
end

def render(name, header, entries)
  out = +""
  header.each_line { |l| out << l }
  out << "static const struct { const char *cls; const char *m; int min; int max;\n"
  out << "                      const char *lo_exp; const char *hi_exp; }\n"
  out << "#{name}[] = {\n"
  entries.each_slice(2) do |sl|
    row = sl.map do |c, m, mn, mx, lo, hi|
      %Q[{"#{c}","#{m}",#{mn},#{mx},#{lo ? "\"#{lo}\"" : "NULL"},#{hi ? "\"#{hi}\"" : "NULL"}}]
    end.join(",")
    out << "  #{row},\n"
  end
  out << "  {NULL, NULL, 0, 0, NULL, NULL}\n};\n"
  out
end

src = File.read(SOURCE)
ver = RUBY_DESCRIPTION.split(" (").first

inst = []
tbl = src[/sp_builtin_arity_tbl\[\] = \{(.*?)\n\};/m, 1] or
  abort "sp_builtin_arity_tbl not found in #{SOURCE}"
tbl.scan(/\{"(\w+)","([^"]+)",-?\d+\}/) do |cls, m|
  recv = INSTANCE_RECEIVERS[cls] or next
  s = spec_for(recv, m)
  inst << [cls, m, *s] if s
end
inst_hdr = <<~C
  /* Positional-arity spec for the builtin instance surface, probed from
     #{ver} by tools/gen_builtin_arity_spec.rb (see there for the
     technique; rerun it with --write to regenerate both tables). max -1 =
     no upper bound; a NULL exp = that side is never violated. Bare calls
     only -- a block changes several counts, and block-carrying calls skip
     the guard. */
C
inst_out = render("sp_builtin_arity_spec_tbl", inst_hdr, inst)

cm = []
CLASS_TARGETS.each do |cls, meths|
  recv = Object.const_get(cls)
  meths.each do |m|
    s = spec_for(recv, m)
    cm << [cls, m, *s] if s
  end
end
cm_hdr = <<~C
  /* Class/module-method positional arity, probed from #{ver} the same
     way as the instance table above (tools/gen_builtin_arity_spec.rb).
     These are the constructors and module functions whose emitters index
     argv[] unconditionally (File.open with no arguments was a compile-time
     SIGSEGV). */
C
cm_out = render("sp_builtin_cmeth_arity_spec_tbl", cm_hdr, cm)

if ARGV.include?("--write")
  wrote = []
  { "instance" => [/\/\* Positional-arity spec for the builtin instance surface.*?\nsp_builtin_arity_spec_tbl\[\] = \{.*?\n\};\n/m, inst_out, inst.length],
    "class-method" => [/\/\* Class\/module-method positional arity.*?\nsp_builtin_cmeth_arity_spec_tbl\[\] = \{.*?\n\};\n/m, cm_out, cm.length],
  }.each do |label, (pat, replacement, count)|
    old = src[pat]
    if old
      src = src.sub(old, replacement)
      wrote << "#{count} #{label}"
    else
      warn "note: no existing #{label} spec table in #{SOURCE}; skipped"
    end
  end
  abort "no spec tables found in #{SOURCE}" if wrote.empty?
  File.write(SOURCE, src)
  warn "wrote #{wrote.join(" + ")} entries into #{SOURCE}"
else
  puts inst_out
  puts
  puts cm_out
  warn "#{inst.length} instance + #{cm.length} class-method entries"
end
