# headers: true (a CSV::Table of CSV::Row), an explicit header array, and the
# :numeric / :integer / :float converters.
require "csv"

t = CSV.parse("a,b\n1,2\n3,4\n", headers: true)
p t.headers
p t.size
t.each { |r| p [r["a"], r["b"], r.to_a] }
p t["a"]
p t[0].to_h
p CSV.parse("a,b\n1,2\n", headers: true).map { |r| r["a"] }
p CSV.parse("x,y", headers: ["h1", "h2"]).map { |r| r.to_h }

p CSV.parse_line("1,2", converters: :numeric)
p CSV.parse("1,2.5,x,-3", converters: :numeric)
p CSV.parse("1,2.5", converters: :integer)
p CSV.parse("1,2.5", converters: :float)

r = CSV::Row.new(["x", "y"], [1, 2])
p [r["x"], r[1], r.to_h, r.fields, r.size, r.header?("y")]
r["z"] = 3
p r.to_h
p r.to_s
