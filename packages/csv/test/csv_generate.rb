# Writing: generate_line quoting rules, the accumulating CSV.generate block,
# and a file round-trip through CSV.open / CSV.read / CSV.foreach.
require "csv"

p CSV.generate_line(["a", nil, "b,c"])
p CSV.generate_line(["q\"uote", "nl\nhere"])
p CSV.generate_line([1, 2], force_quotes: true)
p CSV.generate_line(["a", "b"], col_sep: ";")
p CSV.generate { |csv| csv << ["a", 1] << ["b,c", nil] }

path = "csv_pkg_roundtrip.csv"
CSV.open(path, "w") do |csv|
  csv << ["name", "qty"]
  csv << ["apple, red", 3]
  csv << ["pear \"big\"", nil]
end
p File.read(path)
p CSV.read(path)
p CSV.read(path, headers: true).map { |r| r["name"] }

rows = []
CSV.foreach(path) { |r| rows << r }
p rows

c = CSV.new("a,b\n1,2\n")
p c.shift
p c.read.size
p c.shift
File.delete(path)
