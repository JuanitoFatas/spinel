# spinel-doctor: one health report for a spinel program. Each leg is
# independent; the behavior leg is skipped cleanly when CRuby is absent.
#
# Usage: spinel-doctor [--only a,b] [--skip a,b] [--behavior] [--quiet] app.rb [-- <compiler flags>]
#
# Legs:
#   build       compile to a binary; report compile / codegen / C-build failure
#   unsupported codegen gaps that degrade to a stub (stderr "unsupported ...")
#   unresolved  calls that silently degrade to nil/0 (SPINEL_WARN_UNRESOLVED)
#   inference   methods spinel widened to untyped (the boxed poly slow path)
#   advice      where the boxed slow path will cost: boxed operations in the
#               generated C, ranked by loop depth, mapped back to source lines
#   requires    non-relative `require`s spinel treats as native / no-op
#   behavior    (opt) compiled output vs CRuby; needs `ruby` on PATH

require_relative "tool_common"

# Does this leg run, given --only / --skip?
def leg_on(name, only, skip)
  return false if skip.include?(name)
  return true if only.length == 0
  only.include?(name)
end

# Print a leg's header and its finding lines. Returns the finding count.
def report(name, sev, lines, quiet)
  if lines.length == 0
    puts "  [ok]   " + name if !quiet
    return 0
  end
  mark = "[warn]"
  mark = "[ERR] " if sev == "error"
  mark = "[info]" if sev == "info"
  puts mark + " " + name + " (" + lines.length.to_s + ")"
  lines.each { |l| puts "         " + l }
  lines.length
end

# Keep only the interesting diagnostic lines from a compile run.
def grep_kind(out, needle)
  hits = []
  out.split("\n").each { |l|
    hits.push(l.strip) if l.include?(needle)
  }
  hits
end

# Count non-overlapping occurrences of `needle` in `s`.
def count_in(s, needle)
  n = 0
  i = 0
  last = s.length - needle.length
  while i <= last
    if s[i, needle.length] == needle
      n = n + 1
      i = i + needle.length
    else
      i = i + 1
    end
  end
  n
end

# Is `ch` a C identifier character?
def ident_char(ch)
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_".include?(ch)
end

# The generated function a C definition line opens, or "". A definition line
# carries the name as the one `sp_...` identifier directly followed by `(`
# (the return type ends in a space or `*`, argument names follow a space).
def c_fn_name(line)
  t = " " + line
  best = ""
  i = 0
  while i < t.length - 4
    if t[i, 4] == " sp_"
      j = i + 1
      while j < t.length && ident_char(t[j, 1])
        j = j + 1
      end
      best = t[i + 1, j - i - 1] if j < t.length && t[j, 1] == "("
    end
    i = i + 1
  end
  best
end

# Does `s` end with `sfx`?
def ends_with(s, sfx)
  return false if s.length < sfx.length
  s[s.length - sfx.length, sfx.length] == sfx
end

def main
  only = []
  skip = []
  want_behavior = false
  quiet = false
  src = ""
  extra = ""
  i = 0
  while i < ARGV.length
    a = ARGV[i]
    if a == "--"
      # Everything after -- goes to the compiler verbatim, so the legs report
      # on the program that is actually built (--rbs seeds change how boxed
      # the program is, which is exactly what the perf legs measure) (#4198).
      j = i + 1
      while j < ARGV.length
        extra = extra + " " + ARGV[j]
        j = j + 1
      end
      i = ARGV.length
    elsif a == "--only"
      i = i + 1
      only = ARGV[i].split(",") if i < ARGV.length
    elsif a == "--skip"
      i = i + 1
      skip = ARGV[i].split(",") if i < ARGV.length
    elsif a == "--behavior"
      want_behavior = true
    elsif a == "--quiet"
      quiet = true
    elsif a == "-h" || a == "--help"
      puts "usage: spinel-doctor [--only a,b] [--skip a,b] [--behavior] [--quiet] app.rb [-- <compiler flags>]"
      puts "  legs: build unsupported unresolved inference advice requires behavior"
      puts "  everything after -- is passed to the compiler verbatim (e.g. -- --rbs .)"
      exit(0)
    else
      src = a
    end
    i = i + 1
  end
  die("usage: spinel-doctor [options] app.rb", 2) if src.length == 0
  die("spinel-doctor: no such file: " + src, 2) if !File.exist?(src)

  sp = find_spinel
  cobj = tmp_path("doctor", src, ".c")
  bin = tmp_path("doctor", src, ".bin")
  errs = 0

  puts "spinel-doctor: " + src

  # One codegen-only run with unresolved warnings on feeds three legs.
  diag = sh("SPINEL_WARN_UNRESOLVED=1 " + sp + " " + src + extra + " -c -o " + cobj)
  cg_ok = $sh_status == 0

  if leg_on("unsupported", only, skip)
    errs = errs + report("unsupported", "error", grep_kind(diag, "unsupported "), quiet)
  end
  if leg_on("unresolved", only, skip)
    errs = errs + report("unresolved", "warn", grep_kind(diag, "warning: unresolved"), quiet)
  end

  # Methods whose signature spinel widened to `untyped` -- the boxed poly
  # slow path. Each such site is a "# spinel: widened to untyped (slow path)"
  # comment in --emit-rbs. Shared by the inference leg (which lists them) and
  # the advice leg (which labels hot lines sitting inside one).
  widened = []
  if leg_on("inference", only, skip) || leg_on("advice", only, skip)
    rbs = tmp_path("doctor", src, ".rbs")
    sh(sp + " " + src + extra + " --emit-rbs -o " + rbs)
    if File.exist?(rbs)
      File.read(rbs).split("\n").each { |l|
        widened.push(l.strip) if l.include?("# spinel: widened")
      }
    end
  end

  if leg_on("inference", only, skip)
    # Not a defect (the program is correct), so it is info severity and does
    # not affect the exit code; it surfaces where inference could not pin a
    # concrete type (a perf cost, and sometimes the first sign of a latent
    # type gap worth a closer look).
    report("inference", "info", widened, quiet)
  end

  if leg_on("advice", only, skip) && cg_ok
    # Where the boxed slow path will actually cost. Static analysis cannot
    # measure time, but it can see the two things that predict it: which
    # operations compile to boxed dispatch (a `sp_poly_*` call in the
    # generated C), and how deeply nested in loops each one sits. Bucketing
    # the calls by loop depth and mapping them back through the #line
    # directives ranks the program's own lines by expected cost -- the same
    # triage a profiler would give, without running the program. Advice, not
    # a defect: info severity.
    wnames = []
    widened.each { |l|
      if l.start_with?("def ")
        nm = l[4, l.length].split(":")[0].to_s.strip
        nm = nm.split(".")[1].to_s if nm.include?(".")
        wnames.push(nm) if nm.length > 0
      end
    }
    keys = []
    scores = []
    counts = []
    depths = []
    fns = []
    slot = {}
    cur_line = 0
    cur_file = ""
    bd = 0
    loop_at = []
    fn = ""
    File.read(cobj).split("\n").each { |cl|
      if cl.start_with?("#line ")
        parts = cl.split(" ")
        cur_line = parts[1].to_i
        if parts.length > 2
          f = parts[2].to_s
          cur_file = f[1, f.length - 2].to_s if f.length > 2
        end
      else
        nm = ""
        if bd == 0 && cl.include?("(") && cl.include?("{")
          nm = c_fn_name(cl)
          nm = "main" if nm.length == 0 && cl.include?("int main(")
        end
        fn = nm if nm.length > 0
        n = count_in(cl, "sp_poly_")
        n = 0 if cur_file == "<spinel-synthesized>"
        if n > 0 && cur_line > 0
          d = loop_at.length
          w = 1
          w = 8 if d == 1
          w = 64 if d == 2
          w = 512 if d > 2
          key = cur_file + ":" + cur_line.to_s
          ix = slot[key]
          if ix.nil?
            ix = keys.length
            slot[key] = ix
            keys.push(key)
            scores.push(0)
            counts.push(0)
            depths.push(0)
            fns.push(fn)
          end
          scores[ix] = scores[ix] + n * w
          counts[ix] = counts[ix] + n
          depths[ix] = d if d > depths[ix]
        end
        if (cl.include?("for (") || cl.include?("while (")) && !cl.include?("while (0)")
          loop_at.push(bd)
        end
        bd = bd + count_in(cl, "{") - count_in(cl, "}")
        while loop_at.length > 0 && loop_at[loop_at.length - 1] >= bd
          loop_at.pop
        end
      end
    }
    lines = []
    k = 0
    while k < 8 && k < keys.length
      best = -1
      bs = 0
      j = 0
      while j < keys.length
        if scores[j] > bs
          bs = scores[j]
          best = j
        end
        j = j + 1
      end
      break if best < 0
      scores[best] = -1
      where = "outside any loop"
      where = "in a depth-" + depths[best].to_s + " loop" if depths[best] > 0
      tag = ""
      wnames.each { |wn|
        tag = ", widened to untyped" if tag.length == 0 && ends_with(fns[best], "_" + wn)
      }
      lines.push(keys[best] + ": " + counts[best].to_s + " boxed op(s) " +
                 where + " (" + fns[best] + tag + ")")
      k = k + 1
    end
    lines.push("deepest loops first: steadying a slot's type there removes the boxed dispatch") if lines.length > 0
    report("advice", "info", lines, quiet)
  end

  if leg_on("requires", only, skip)
    reqs = []
    File.read(src).split("\n").each { |line|
      s = line.strip
      if s.start_with?("require ") && !s.start_with?("require_relative")
        reqs.push(s)
      end
    }
    report("requires", "info", reqs, quiet)
  end

  if leg_on("build", only, skip)
    out = sh(sp + " " + src + extra + " -o " + bin + " --line-map")
    if $sh_status == 0
      report("build", "error", [], quiet)
    else
      lines = []
      out.split("\n").each { |l| lines.push(l.strip) if l.strip.length > 0 }
      errs = errs + report("build", "error", lines, quiet)
    end
  end

  if leg_on("behavior", only, skip) && (want_behavior || have_cmd("ruby"))
    if !have_cmd("ruby")
      puts "  [skip] behavior (needs ruby)" if !quiet
    elsif !File.exist?(bin)
      puts "  [skip] behavior (build produced no binary)" if !quiet
    else
      ref = sh("ruby " + src)
      got = sh(bin)
      if ref == got
        report("behavior", "error", [], quiet)
      else
        errs = errs + report("behavior", "error",
          ["compiled output differs from CRuby (diff the two runs to localize)"], quiet)
      end
    end
  end

  puts ""
  if errs == 0
    puts "spinel-doctor: clean"
    exit(0)
  end
  puts "spinel-doctor: " + errs.to_s + " finding(s) at error severity"
  exit(1)
end

main
