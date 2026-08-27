#!/usr/bin/env ruby

# Probe every builtin positional argument slot for the implicit conversion
# protocol, then check which of those slots Spinel compiles:
#
#   ruby tools/gen_conv_slot_probe.rb [report.json]   (SPINEL=bin/spinel, JOBS=n)
#
# Technique, per (class, method), the same as tools/gen_nil_arg_probe.rb:
#
#   1. Find a WORKING baseline call in CRuby for each accepted count.
#   2. For each slot whose baseline value is a String, Integer, Array or Hash,
#      substitute a user object answering the slot's implicit conversion
#      (#to_str, #to_int, #to_ary, #to_hash) and, separately, a wrong-class
#      scalar (an Integer in a String slot, a String in an Integer slot).
#      Record what CRuby answers: the result's inspect, or the error.
#   3. Compile and run the same program with Spinel (`spinel -E`) and classify:
#      "match", "cfail" (the generated C does not compile -- the slot takes
#      the raw value), or "differ" (a run-time divergence).
#
# The report drives a sweep of the typed-slot emitters in src/codegen*.c: every
# "cfail" is a slot still emitted with emit_expr where emit_str_expr /
# emit_int_expr (or the Array / Hash protocols) belong.
#
# Probing runs inside a fresh temp directory. The fd-level forms are never
# a baseline (File.open(1) would hand the probe its own stdout), and
# Kernel#system is probed with `true` as its command only.

require "timeout"
require "json"
require "tmpdir"
require "stringio"
require "strscan"
require "pathname"
require "set"
require "etc"

Warning[:deprecated] = false

ROOT = File.expand_path("..", __dir__)
SOURCE = File.join(ROOT, "src/codegen_call.c")
OUT = File.expand_path(ARGV.fetch(0, "conv_slot_probe.json"))
SPINEL = File.expand_path(ENV.fetch("SPINEL", File.join(ROOT, "bin/spinel")))
JOBS = Integer(ENV.fetch("JOBS", Etc.nprocessors))

# receiver source text (compiled by Spinel) and the matching CRuby thunk
INSTANCE_RECEIVERS = {
  "String" => ['"ab".dup', -> { "ab".dup }],
  "Integer" => ["1", -> { 1 }],
  "Float" => ["1.0", -> { 1.0 }],
  "Symbol" => [":a", -> { :a }],
  "Array" => ["[1, 2]", -> { [1, 2] }],
  "Hash" => ["{1 => 2}", -> { {1 => 2} }],
  "Range" => ["(1..2)", -> { (1..2) }],
  "Time" => ["Time.at(0)", -> { Time.at(0) }],
  "StringIO" => ['StringIO.new("ab".dup)', -> { StringIO.new("ab".dup) }],
  "StringScanner" => ['StringScanner.new("ab")', -> { StringScanner.new("ab") }],
  "Pathname" => ['Pathname.new("a")', -> { Pathname.new("a") }],
  "Set" => ["Set.new([1, 2])", -> { Set.new([1, 2]) }],
}

CLASS_TARGETS = {
  "File"    => %w[open new read write binread binwrite readlines foreach delete unlink
                  rename exist? size basename dirname extname join split expand_path
                  chmod utime truncate symlink link readlink realpath stat lstat
                  ftype mtime atime ctime empty? zero? identical? absolute_path
                  realdirpath birthtime readable? writable? executable? file?
                  directory? readable_real? writable_real? executable_real?
                  world_readable? world_writable? owned? grpowned? setuid? setgid?
                  sticky? socket? blockdev? chardev? pipe? symlink? fnmatch fnmatch?
                  mkfifo lchmod],
  "FileTest" => %w[exist? size file? directory? readable? writable? executable?
                   zero? empty? owned? grpowned? setuid? setgid? sticky? socket?
                   blockdev? chardev? pipe? symlink? identical? world_readable?
                   world_writable?],
  "Dir"     => %w[new open mkdir rmdir delete unlink entries children glob foreach
                  exist? empty? each_child],
  "IO"      => %w[read write binread binwrite readlines foreach],
  "Time"    => %w[at local mktime utc gm],
  "Hash"    => %w[new],
  "Array"   => %w[new],
  "String"  => %w[new],
  "Integer" => %w[sqrt],
  "Math"    => %w[sqrt cbrt sin cos tan atan2 log log2 log10 exp hypot ldexp],
  "Regexp"  => %w[new escape quote union],
  "Random"  => %w[new rand srand],
  "ENV"     => %w[fetch key? has_key? include? member? assoc rassoc values_at
                  delete store [] []=],
  "Kernel"  => %w[format sprintf putc print puts p system],
  "Marshal" => %w[dump load],
}

SAMPLES = [1, "a", :a, [1], {1 => 2}, 0.5, (0..1), /a/]

# The user classes substituted into a slot, by the baseline value's class; the
# wrapped value is the baseline value itself so a passing probe answers the
# baseline's result.
CONVERSIONS = {
  String => "to_str", Integer => "to_int", Array => "to_ary", Hash => "to_hash",
}
WRONG_SCALAR = { String => 1, Integer => "a" }

def reset_fixture
  File.chmod(0o644, "a") rescue nil
  File.delete("a") rescue nil
  File.binwrite("a", "hello")
rescue SystemCallError
end

def attempt(recv_thunk, m, args, with_block: false)
  reset_fixture
  r = recv_thunk.call
  res = Timeout.timeout(2) do
    with_block ? r.__send__(m, *args) { |*| "a" } : r.__send__(m, *args)
  end
  return attempt(recv_thunk, m, args, with_block: true) if !with_block && res.is_a?(Enumerator)
  [:ok, res, with_block]
rescue Exception => e
  e
end

def baseline_at(recv_thunk, m, n)
  return [] if n == 0 && attempt(recv_thunk, m, []).is_a?(Array)
  return nil if n == 0
  SAMPLES.repeated_permutation(n) do |args|
    r = attempt(recv_thunk, m, args)
    next unless r.is_a?(Array)
    # an Integer where a path belongs is the fd form: File.open(1) hands back
    # the probe's OWN stdout, and closing it when the wrapper is collected
    # takes the probe's output with it. Never a baseline; and close what a
    # real path form opened, so the search does not leak a descriptor per
    # permutation.
    if r[1].is_a?(IO)
      r[1].close unless r[1].closed? || r[1].fileno < 3
      next if args.any?(Integer)
    end
    return args
  end
  nil
end

def accepted_counts(recv_thunk, m)
  (0..3).select do |n|
    r = attempt(recv_thunk, m, Array.new(n))
    !(r.is_a?(ArgumentError) && r.message =~ /wrong number of arguments/)
  end
end

# A probe's outcome as text both runtimes can be compared on.
def outcome_text(r)
  return "#{r.class}: #{r.message}"[0, 160] unless r.is_a?(Array)
  v = r[1]
  s = v.inspect
  s = s.gsub(/0x[0-9a-f]+/, "0xADDR")
  s[0, 160]
end

class Wrapped
  def initialize(v, m)
    @v = v
    @m = m
  end

  def respond_to_missing?(name, priv = false)
    name.to_s == @m || super
  end

  def method_missing(name, *args)
    return @v if name.to_s == @m
    super
  end
end

# CRuby side: for each slot, the conversion and wrong-scalar outcomes.
def probe_method(recv_thunk, m)
  return nil unless recv_thunk.call.respond_to?(m)
  counts = accepted_counts(recv_thunk, m)
  return nil if counts.empty?
  by_count = {}
  counts.sort.reverse_each do |n|
    next if n == 0
    args = baseline_at(recv_thunk, m, n)
    next if args.nil?
    base = attempt(recv_thunk, m, args)
    slots = args.each_index.map do |k|
      conv = CONVERSIONS[args[k].class]
      next nil unless conv
      sub = args.dup
      sub[k] = Wrapped.new(args[k], conv)
      c = attempt(recv_thunk, m, sub)
      wrong = WRONG_SCALAR[args[k].class]
      sub2 = args.dup
      sub2[k] = wrong
      w = attempt(recv_thunk, m, sub2)
      {"conv" => conv, "conv_out" => outcome_text(c),
       "conv_ok" => c.is_a?(Array) && outcome_text(c) == outcome_text(base),
       "wrong" => wrong.inspect, "wrong_out" => outcome_text(w)}
    end
    next if slots.compact.empty?
    by_count[n] = {"baseline" => args.map(&:inspect), "block" => base[2],
                   "base_out" => outcome_text(base), "slots" => slots}
  end
  by_count.empty? ? nil : by_count
end

# The Spinel-side program for one probe.
def program(recv_src, m, info, k, kind)
  args = info["baseline"].dup
  slot = info["slots"][k]
  pre = ""
  if kind == "conv"
    conv = slot["conv"]
    pre = "class Conv\n  def initialize(v)\n    @v = v\n  end\n\n  def #{conv}\n    @v\n  end\nend\n"
    args[k] = "Conv.new(#{args[k]})"
  else
    args[k] = slot["wrong"]
  end
  if m =~ /\A[a-z_]\w*[?!]?\z/
    call = "#{recv_src}.#{m}(#{args.join(', ')})"
  elsif m == "[]"
    call = "#{recv_src}[#{args.join(', ')}]"
  elsif m == "[]=" && args.size == 2
    call = "#{recv_src}[#{args[0]}] = #{args[1]}"
  elsif args.size == 1 && m =~ /\A[-+*\/%<>=!&|^~]+\z/
    call = "#{recv_src} #{m} #{args[0]}"
  else
    return nil
  end
  call += ' { |*| "a" }' if info["block"]
  <<~RUBY
    require "stringio"
    require "strscan"
    require "pathname"
    require "set"
    #{pre}
    File.binwrite("a", "hello")
    begin
      r = #{call}
      puts "SP-RESULT \#{r.inspect.gsub(/0x[0-9a-f]+/, "0xADDR")}"
    rescue Exception => e
      puts "SP-RESULT \#{e.class}: \#{e.message}"
    end
  RUBY
end

# Each probe gets its own directory: File.open("a", "a", 1) leaves the shared
# fixture mode 0o001 and every concurrent path-taking probe then sees EACCES.
def run_spinel(src, root, idx)
  dir = File.join(root, "p#{idx}")
  Dir.mkdir(dir)
  path = File.join(dir, "p#{idx}.rb")
  File.write(path, src)
  out = File.join(dir, "p#{idx}.out")
  err = File.join(dir, "p#{idx}.err")
  pid = Process.spawn(SPINEL, "-E", path, chdir: dir, in: File::NULL, out: out, err: err)
  begin
    Timeout.timeout(60) { Process.wait(pid) }
  rescue Timeout::Error
    Process.kill("KILL", pid) rescue nil
    Process.wait(pid) rescue nil
    return ["timeout", ""]
  end
  # the program's own output (puts / print probes) precedes the sentinel line
  res = File.read(out).lines.find { |l| l.start_with?("SP-RESULT ") }
  e = File.read(err)
  if $?.exitstatus != 0 && res.nil?
    line = e.lines.find { |l| l =~ /error:/ }
    return [line ? "cfail" : "reject", (line || e.lines.first || "").strip[0, 200]]
  end
  [res ? res.delete_prefix("SP-RESULT ").strip[0, 160] : "", ""]
end

report = {}
src = File.read(SOURCE)
tbl = src[/sp_builtin_arity_tbl\[\] = \{(.*?)\n\};/m, 1] or
  abort "sp_builtin_arity_tbl not found in #{SOURCE}"
surface = Hash.new { |h, k| h[k] = [] }
tbl.scan(/\{"(\w+)","([^"]+)",-?\d+\}/) { |cls, m| surface[cls] << m }
# pread: probing StringIO#pread with mistyped arguments segfaults CRuby
# 4.0.6 itself ([BUG] in pread); the fd-level forms stay off the surface.
%w[StringIO StringScanner Pathname Set].each do |cls|
  inst = INSTANCE_RECEIVERS[cls][1].call
  surface[cls] = inst.public_methods(false).map(&:to_s).sort -
                 %w[pretty_print pretty_print_cycle pread pwrite sysread syswrite
                    readpartial read_nonblock write_nonblock sysseek ioctl fcntl
                    set_encoding_by_bom reopen]
end
surface.each_value(&:uniq!)

probes = []
Dir.mktmpdir("conv_probe") do |dir|
  Dir.chdir(dir) do
    surface.each do |cls, meths|
      recv = INSTANCE_RECEIVERS[cls] or next
      meths.each do |m|
        next if m =~ /\A(instance_|__|define_|method_missing|send|public_send|freeze|object_id|display|pretty)/
        r = probe_method(recv[1], m) rescue nil
        report["#{cls}##{m}"] = r if r
      end
    end
    CLASS_TARGETS.each do |cls, meths|
      const = Object.const_get(cls)
      meths.each do |m|
        r = probe_method(-> { const }, m) rescue nil
        report["#{cls}.#{m}"] = r if r
      end
    end
  end
end
warn "#{report.size} methods probed in CRuby"

report.each do |key, counts|
  cls, sep, m = key.partition(/[#.]/)
  recv_src = sep == "#" ? INSTANCE_RECEIVERS[cls][0] : cls
  counts.each do |n, info|
    info["slots"].each_with_index do |slot, k|
      next unless slot
      # a conversion the method honours, and a wrong scalar whatever CRuby does
      pc = program(recv_src, m, info, k, "conv")
      pw = program(recv_src, m, info, k, "wrong")
      unless pc
        warn "cannot render #{key}: skipped"
        next
      end
      probes << [key, n, k, "conv", pc, slot["conv_out"]] if slot["conv_ok"]
      probes << [key, n, k, "wrong", pw, slot["wrong_out"]]
    end
  end
end
warn "#{probes.size} Spinel probes, #{JOBS} jobs"

results = Array.new(probes.size)
Dir.mktmpdir("conv_spinel") do |dir|
  queue = Queue.new
  probes.each_index { |i| queue << i }
  JOBS.times.map do
    Thread.new do
      loop do
        i = queue.pop(true) rescue nil
        break if i.nil?
        key, n, k, kind, prog, expect = probes[i]
        out, detail = run_spinel(prog, dir, i)
        status = case out
                 when "cfail", "reject", "timeout" then out
                 else out == expect ? "match" : "differ"
                 end
        results[i] = {"method" => key, "count" => n, "slot" => k, "kind" => kind,
                      "status" => status, "expect" => expect, "got" => out,
                      "detail" => detail}
        $stderr.print "." if i % 50 == 0
      end
    end
  end.each(&:join)
end
warn ""

File.write(OUT, JSON.pretty_generate({"cruby" => report, "probes" => results}))
tally = results.group_by { |r| [r["kind"], r["status"]] }.transform_values(&:size)
warn tally.sort.map { |(kd, st), n| "#{kd}/#{st}=#{n}" }.join("  ")
results.select { |r| r["status"] == "cfail" }.each do |r|
  warn "  cfail #{r['kind']} #{r['method']}/#{r['count']} slot #{r['slot']}: #{r['detail'][0, 110]}"
end
warn "-> #{OUT}"
