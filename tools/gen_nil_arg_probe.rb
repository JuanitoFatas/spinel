#!/usr/bin/env ruby

# Probe CRuby for nil/true/false acceptance at builtin positional argument
# slots, to drive campaign 3's strict-argument validation:
#
#   ruby tools/gen_nil_arg_probe.rb [report.json]
#
# Technique, per (class, method):
#
#   1. Find a WORKING baseline call: for each accepted count n (0..3), search
#      small sample values (1, "a", :a, [1], {1=>2}, 0.5, ranges, regexps)
#      for a combination the method accepts without raising. Methods that
#      never accept any combination are reported unprobed rather than guessed.
#   2. Substitute nil, true, and false at each slot of the working call, one
#      slot at a time, and record the outcome: :ok (the value is accepted) or
#      the error class and message CRuby raises.
#
# The report feeds two decisions in src/codegen*.c:
#   - which call sites keep the LENIENT typed-slot emitters (CRuby accepts
#     nil there: "x".split(nil), IO.read(path, nil), ...), and
#   - the exact TypeError wording for everything else.
#
# Probing runs inside a fresh temp directory (file-touching methods create
# and rename real files); methods that could reach outside the process's
# sandbox (Process.*, fd-taking IO constructors, chdir) are excluded.

require "timeout"
require "json"
require "tmpdir"
require "stringio"
require "strscan"
require "pathname"
require "set"

Warning[:deprecated] = false

ROOT = File.expand_path("..", __dir__)
SOURCE = File.join(ROOT, "src/codegen_call.c")
OUT = File.expand_path(ARGV.fetch(0, "nil_arg_probe.json"))

INSTANCE_RECEIVERS = {
  "String" => -> { "ab".dup }, "Integer" => -> { 1 }, "Float" => -> { 1.0 },
  "Symbol" => -> { :a }, "Array" => -> { [1, 2] }, "Hash" => -> { {1 => 2} },
  "Range" => -> { (1..2) }, "Time" => -> { Time.at(0) },
  "StringIO" => -> { StringIO.new("ab".dup) },
  "StringScanner" => -> { StringScanner.new("ab") },
  "Pathname" => -> { Pathname.new("a") },
  "Set" => -> { Set.new([1, 2]) },
}

CLASS_TARGETS = {
  "File"    => %w[open new read write binread binwrite readlines foreach delete unlink
                  rename exist? size basename dirname extname join split expand_path
                  chmod utime truncate symlink link readlink realpath stat lstat
                  ftype mtime atime ctime empty? zero? identical? absolute_path],
  "Dir"     => %w[new open mkdir rmdir delete unlink entries children glob foreach
                  exist? empty? pwd getwd],
  "Time"    => %w[at local mktime utc gm],
  "Hash"    => %w[new],
  "Array"   => %w[new],
  "String"  => %w[new],
  "Integer" => %w[sqrt],
  "Math"    => %w[sqrt cbrt sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh
                  atanh log log2 log10 exp hypot ldexp frexp erf erfc gamma lgamma],
  "Regexp"  => %w[new escape quote union],
  "Random"  => %w[new rand srand],
  "ENV"     => %w[fetch key? has_key? include? member? assoc rassoc values_at],
  "Kernel"  => %w[format sprintf Integer Float String Array Hash Rational Complex],
  "Marshal" => %w[dump load],
}

SAMPLES = [1, "a", :a, [1], {1 => 2}, 0.5, (0..1), /a/]

# A blockless call on an enumerator-returning method validates nothing (the
# Enumerator is lazy: "ab".gsub(nil) succeeds); when the bare call yields an
# Enumerator, re-run with a block so the arguments are actually consumed.
def attempt(recv_thunk, m, args, with_block: false)
  r = recv_thunk.call
  res = Timeout.timeout(2) do
    with_block ? r.__send__(m, *args) { |*| "a" } : r.__send__(m, *args)
  end
  return attempt(recv_thunk, m, args, with_block: true) if !with_block && res.is_a?(Enumerator)
  :ok
rescue Exception => e
  e
end

# Search for a working sample combination at the given count.
def baseline_at(recv_thunk, m, n)
  return [] if n == 0 && attempt(recv_thunk, m, []) == :ok
  return nil if n == 0
  SAMPLES.repeated_permutation(n) do |args|
    return args if attempt(recv_thunk, m, args) == :ok
  end
  nil
end

def accepted_counts(recv_thunk, m)
  (0..3).select do |n|
    r = attempt(recv_thunk, m, Array.new(n))
    !(r.is_a?(ArgumentError) && r.message =~ /wrong number of arguments/)
  end
end

# Probe every accepted count (largest first, so optional slots are tested:
# "x".split(nil) is only reachable at count 1), one result set per count.
def probe_method(recv_thunk, m)
  return nil unless recv_thunk.call.respond_to?(m)
  counts = accepted_counts(recv_thunk, m)
  return nil if counts.empty?
  by_count = {}
  found_any = false
  counts.sort.reverse_each do |n|
    next if n == 0
    args = baseline_at(recv_thunk, m, n)
    next if args.nil?
    found_any = true
    slots = args.each_index.map do |k|
      [nil, true, false].map do |v|
        sub = args.dup
        sub[k] = v
        r = attempt(recv_thunk, m, sub)
        if r == :ok then "ok"
        else "#{r.class}: #{r.message}"[0, 120]
        end
      end
    end
    by_count[n] = {"baseline" => args.map(&:inspect), "slots" => slots}
  end
  return nil if by_count.empty? && counts == [0]  # nothing positional to test
  return {"unprobed" => true} unless found_any
  by_count
end

report = {}

src = File.read(SOURCE)
tbl = src[/sp_builtin_arity_tbl\[\] = \{(.*?)\n\};/m, 1] or
  abort "sp_builtin_arity_tbl not found in #{SOURCE}"
surface = Hash.new { |h, k| h[k] = [] }
tbl.scan(/\{"(\w+)","([^"]+)",-?\d+\}/) { |cls, m| surface[cls] << m }
# the native receivers are not in the dump; probe their public instance surface
# pread: probing StringIO#pread with mistyped arguments segfaults CRuby
# 4.0.6 itself ([BUG] in pread); the fd-level forms stay off the surface.
%w[StringIO StringScanner Pathname Set].each do |cls|
  inst = INSTANCE_RECEIVERS[cls].call
  surface[cls] = inst.public_methods(false).map(&:to_s).sort -
                 %w[pretty_print pretty_print_cycle pread pwrite sysread syswrite
                    readpartial read_nonblock write_nonblock sysseek ioctl fcntl]
end

Dir.mktmpdir("nil_probe") do |dir|
  Dir.chdir(dir) do
    surface.each do |cls, meths|
      thunk = INSTANCE_RECEIVERS[cls] or next
      meths.each do |m|
        r = probe_method(thunk, m) rescue nil
        report["#{cls}##{m}"] = r if r
      end
    end
    CLASS_TARGETS.each do |cls, meths|
      const = Object.const_get(cls)
      thunk = -> { const }
      meths.each do |m|
        r = probe_method(thunk, m) rescue nil
        report["#{cls}.#{m}"] = r if r
      end
    end
  end
end

File.write(OUT, JSON.pretty_generate(report))

warn "#{report.size} methods probed -> #{OUT}"
report.each do |k, counts|
  next if counts["unprobed"]
  counts.each do |n, v|
    slots = v["slots"].each_with_index.select { |s, _| s[0] == "ok" }.map(&:last)
    next if slots.empty?
    warn "  #{k}/#{n} (baseline #{v["baseline"].join(", ")}; nil ok at slot #{slots.join(",")})"
  end
end
