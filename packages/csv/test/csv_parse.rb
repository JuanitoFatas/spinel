# The grammar: fields separated by col_sep, rows by LF or CRLF, quoted fields
# with a doubled quote for a literal one. An empty UNQUOTED field is nil and an
# empty quoted one is "".
require "csv"

p CSV.parse_line("a,,b")
p CSV.parse_line("a,\"\",b")
p CSV.parse("a,b\nc,d\n")
p CSV.parse("a,b\nc,d")
p CSV.parse("")
p CSV.parse("\n")
p CSV.parse_line("")
p CSV.parse("a,\"x\"\"y\",c")
p CSV.parse("a,\"x\ny\",c")
p CSV.parse("a,b\r\nc,d\r\n")
p CSV.parse("a;b", col_sep: ";")
p CSV.parse("a||b\r\nc||d\r\n", col_sep: "||")
p CSV.parse_line("  spaced , y ")
p CSV.parse("a,b\n\nc,d\n")
p CSV.parse("a,b\n\nc,d\n", skip_blanks: true)

rows = []
CSV.parse("p,1\nq,2\n") { |row| rows << row }
p rows

begin
  CSV.parse("\"unterminated")
rescue CSV::MalformedCSVError => e
  p e.class
end
