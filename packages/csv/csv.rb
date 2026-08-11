# Spinel bundled `csv`.
#
# A CSV reader/writer over the RFC 4180 grammar CRuby's csv implements: fields
# separated by col_sep, rows by newline (LF or CRLF), a field optionally quoted
# with quote_char, and a doubled quote_char inside a quoted field standing for
# one literal quote. An empty UNQUOTED field reads as nil and an empty quoted
# one as "", exactly as CRuby answers them.
#
# Covered: CSV.parse / parse_line / read / foreach / generate / generate_line /
# open, the instance reader (#each / #shift / #read / #<<), headers (true or an
# explicit array) with CSV::Row and CSV::Table, :numeric / :integer / :float
# converters, skip_blanks and force_quotes.
#
# Not covered (they raise or are ignored rather than silently misbehave):
# converters by name beyond the numeric family, header converters, :liberal_
# parsing, per-instance encodings, and the by_col/by_row Table access modes.

class CSV
  # Raised for input the grammar cannot accept (an unterminated quoted field).
  class MalformedCSVError < RuntimeError
  end

  # One parsed row when headers are in play: the fields, addressable by header
  # name as well as by index.
  class Row
    attr_reader :headers

    def initialize(headers, fields)
      @headers = headers
      @fields = fields
    end

    def fields
      @fields
    end

    def [](key)
      if key.is_a?(Integer)
        @fields[key]
      else
        i = @headers.index(key)
        i.nil? ? nil : @fields[i]
      end
    end

    def []=(key, value)
      if key.is_a?(Integer)
        @fields[key] = value
      else
        i = @headers.index(key)
        if i.nil?
          @headers << key
          @fields << value
        else
          @fields[i] = value
        end
      end
    end

    def each
      i = 0
      while i < @headers.size
        yield @headers[i], @fields[i]
        i += 1
      end
      self
    end

    def size
      @fields.size
    end

    def length
      @fields.size
    end

    def header?(name)
      @headers.include?(name)
    end

    def include?(name)
      @headers.include?(name)
    end

    def to_a
      pairs = []
      i = 0
      while i < @headers.size
        pairs << [@headers[i], @fields[i]]
        i += 1
      end
      pairs
    end

    def to_h
      h = {}
      i = 0
      while i < @headers.size
        h[@headers[i]] = @fields[i]
        i += 1
      end
      h
    end

    def to_hash
      to_h
    end

    def to_csv
      CSV.generate_line(@fields)
    end

    def to_s
      to_csv
    end

    def ==(other)
      return false unless other.is_a?(Row)
      headers == other.headers && fields == other.fields
    end

    def inspect
      "#<CSV::Row #{to_h.inspect}>"
    end
  end

  # The rows of a headered parse, with the header list kept alongside.
  class Table
    include Enumerable

    def initialize(rows, headers)
      @rows = rows
      @headers = headers
    end

    def headers
      @headers
    end

    def each
      @rows.each { |r| yield r }
      self
    end

    def [](index)
      if index.is_a?(Integer)
        @rows[index]
      else
        @rows.map { |r| r[index] }
      end
    end

    def <<(row)
      @rows << (row.is_a?(Row) ? row : Row.new(@headers, row))
      self
    end

    def size
      @rows.size
    end

    def length
      @rows.size
    end

    def count
      @rows.size
    end

    def empty?
      @rows.empty?
    end

    def first
      @rows.first
    end

    def last
      @rows.last
    end

    def rows
      @rows
    end

    def to_a
      out = [@headers]
      @rows.each { |r| out << r.fields }
      out
    end

    def to_csv
      s = String.new
      s << CSV.generate_line(@headers)
      @rows.each { |r| s << CSV.generate_line(r.fields) }
      s
    end

    def to_s
      to_csv
    end

    def inspect
      "#<CSV::Table mode:col_or_row row_count:#{@rows.size + 1}>"
    end
  end

  # ---- parsing ----

  # Split `str` into rows of raw fields. An unquoted empty field is nil; a
  # quoted one is "". The row terminator is LF or CRLF, and a newline inside a
  # quoted field belongs to the field.
  def self.split_rows(str, col_sep, quote_char, skip_blanks)
    rows = []
    row = []
    field = String.new
    quoted = false
    in_quotes = false
    started = false
    sep_len = col_sep.length
    i = 0
    n = str.length

    while i < n
      ch = str[i]

      if in_quotes
        if ch == quote_char
          if i + 1 < n && str[i + 1] == quote_char
            field << quote_char
            i += 2
          else
            in_quotes = false
            i += 1
          end
        else
          field << ch
          i += 1
        end
        next
      end

      if ch == quote_char
        in_quotes = true
        quoted = true
        started = true
        i += 1
        next
      end

      if ch == col_sep[0] && (sep_len == 1 || str[i, sep_len] == col_sep)
        row << (quoted || field.length > 0 ? field : nil)
        field = String.new
        quoted = false
        started = true
        i += sep_len
        next
      end

      if ch == "\n" || ch == "\r"
        if started || field.length > 0
          row << (quoted || field.length > 0 ? field : nil)
        end
        if !(skip_blanks && row.empty?)
          rows << row
        end
        row = []
        field = String.new
        quoted = false
        started = false
        i += (ch == "\r" && i + 1 < n && str[i + 1] == "\n") ? 2 : 1
        next
      end

      field << ch
      started = true
      i += 1
    end

    raise MalformedCSVError.new("Unclosed quoted field") if in_quotes

    if started || field.length > 0
      row << (quoted || field.length > 0 ? field : nil)
    end
    if !row.empty? && !(skip_blanks && row.empty?)
      rows << row
    end

    rows
  end

  # :numeric / :integer / :float on one field.
  def self.convert_field(value, converters)
    return value if value.nil? || converters.nil?
    if converters == :integer || converters == :numeric
      if value.match?(/\A[+-]?\d+\z/)
        return value.to_i
      end
    end
    if converters == :float || converters == :numeric
      if value.match?(/\A[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?\z/)
        return value.to_f
      end
    end
    value
  end

  def self.convert_row(fields, converters)
    return fields if converters.nil?
    fields.map { |f| convert_field(f, converters) }
  end

  # CSV.parse(str) -> rows; with headers: a CSV::Table. A block is handed each
  # row (or CSV::Row) and the call answers nil, as CRuby does.
  def self.parse(str, col_sep: ",", quote_char: "\"", headers: false,
                 skip_blanks: false, converters: nil)
    raw = split_rows(str, col_sep, quote_char, skip_blanks)

    hdrs = nil
    body = []
    if headers == true
      hdrs = raw.empty? ? [] : raw[0]
      i = 1
      while i < raw.size
        body << raw[i]
        i += 1
      end
    elsif headers.is_a?(Array)
      hdrs = headers
      body = raw
    else
      body = raw
    end

    if hdrs.nil?
      if block_given?
        body.each { |r| yield convert_row(r, converters) }
        return nil
      end
      return body.map { |r| convert_row(r, converters) }
    end

    rows = body.map { |r| Row.new(hdrs, convert_row(r, converters)) }
    if block_given?
      rows.each { |r| yield r }
      return nil
    end
    Table.new(rows, hdrs)
  end

  # The first row of `str`, or nil when it holds none.
  def self.parse_line(str, col_sep: ",", quote_char: "\"", headers: false,
                      skip_blanks: false, converters: nil)
    rows = split_rows(str, col_sep, quote_char, skip_blanks)
    return nil if rows.empty?
    if headers.is_a?(Array)
      return Row.new(headers, convert_row(rows[0], converters))
    end
    convert_row(rows[0], converters)
  end

  def self.read(path, col_sep: ",", quote_char: "\"", headers: false,
                skip_blanks: false, converters: nil)
    parse(File.read(path), col_sep: col_sep, quote_char: quote_char,
          headers: headers, skip_blanks: skip_blanks, converters: converters)
  end

  def self.readlines(path, col_sep: ",", quote_char: "\"", headers: false,
                     skip_blanks: false, converters: nil)
    read(path, col_sep: col_sep, quote_char: quote_char, headers: headers,
         skip_blanks: skip_blanks, converters: converters)
  end

  # CSV.foreach(path) { |row| ... } -- the rows of a file, one at a time.
  def self.foreach(path, col_sep: ",", quote_char: "\"", headers: false,
                   skip_blanks: false, converters: nil)
    data = File.read(path)
    raw = split_rows(data, col_sep, quote_char, skip_blanks)
    if headers == true
      hdrs = raw.empty? ? [] : raw[0]
      i = 1
      while i < raw.size
        yield Row.new(hdrs, convert_row(raw[i], converters))
        i += 1
      end
    elsif headers.is_a?(Array)
      raw.each { |r| yield Row.new(headers, convert_row(r, converters)) }
    else
      raw.each { |r| yield convert_row(r, converters) }
    end
    nil
  end

  # ---- generating ----

  def self.quote_field(value, col_sep, quote_char, force_quotes)
    return "" if value.nil?
    s = value.to_s
    need = force_quotes ||
           s.include?(col_sep) || s.include?(quote_char) ||
           s.include?("\n") || s.include?("\r")
    return s unless need
    out = String.new
    out << quote_char
    i = 0
    while i < s.length
      ch = s[i]
      out << quote_char if ch == quote_char
      out << ch
      i += 1
    end
    out << quote_char
    out
  end

  # One CSV line (with its row separator) for `row`.
  def self.generate_line(row, col_sep: ",", quote_char: "\"", row_sep: "\n",
                         force_quotes: false)
    fields = row.is_a?(Row) ? row.fields : row
    out = String.new
    i = 0
    while i < fields.size
      out << col_sep if i > 0
      out << quote_field(fields[i], col_sep, quote_char, force_quotes)
      i += 1
    end
    out << row_sep
    out
  end

  # CSV.generate { |csv| csv << row } -- the accumulated string.
  def self.generate(str = "", col_sep: ",", quote_char: "\"", row_sep: "\n",
                    force_quotes: false)
    csv = new(String.new(str), col_sep: col_sep, quote_char: quote_char,
              row_sep: row_sep, force_quotes: force_quotes)
    yield csv
    csv.string
  end

  # CSV.open(path, "w") { |csv| csv << row } / CSV.open(path) { |csv| csv.each }
  def self.open(path, mode = "r", col_sep: ",", quote_char: "\"", row_sep: "\n",
                force_quotes: false, headers: false, skip_blanks: false,
                converters: nil)
    reading = mode.start_with?("r")
    data = reading ? File.read(path) : String.new
    csv = new(data, col_sep: col_sep, quote_char: quote_char,
              row_sep: row_sep, force_quotes: force_quotes, headers: headers,
              skip_blanks: skip_blanks, converters: converters)
    result = yield csv
    File.write(path, csv.string) unless reading
    result
  end

  # ---- instance ----

  # A reader over `data` (a String), and a writer accumulating into it.
  def initialize(data = "", col_sep: ",", quote_char: "\"", row_sep: "\n",
                 force_quotes: false, headers: false, skip_blanks: false,
                 converters: nil)
    @string = String.new(data)
    @col_sep = col_sep
    @quote_char = quote_char
    @row_sep = row_sep
    @force_quotes = force_quotes
    @headers = headers
    @skip_blanks = skip_blanks
    @converters = converters
    @rows = nil
    @pos = 0
  end

  def string
    @string
  end

  def col_sep
    @col_sep
  end

  def quote_char
    @quote_char
  end

  def rows
    if @rows.nil?
      raw = CSV.split_rows(@string, @col_sep, @quote_char, @skip_blanks)
      if @headers == true
        hdrs = raw.empty? ? [] : raw[0]
        out = []
        i = 1
        while i < raw.size
          out << CSV::Row.new(hdrs, CSV.convert_row(raw[i], @converters))
          i += 1
        end
        @rows = out
      elsif @headers.is_a?(Array)
        @rows = raw.map { |r| CSV::Row.new(@headers, CSV.convert_row(r, @converters)) }
      else
        @rows = raw.map { |r| CSV.convert_row(r, @converters) }
      end
    end
    @rows
  end

  def shift
    rs = rows
    return nil if @pos >= rs.size
    r = rs[@pos]
    @pos += 1
    r
  end

  def gets
    shift
  end

  def readline
    shift
  end

  # The reader is a stream: #each and #read start where the last #shift left
  # off and consume what they hand back, as CRuby's do.
  def each
    rs = rows
    while @pos < rs.size
      r = rs[@pos]
      @pos += 1
      yield r
    end
    self
  end

  def read
    rs = rows
    out = []
    while @pos < rs.size
      out << rs[@pos]
      @pos += 1
    end
    out
  end

  def to_a
    read
  end

  def rewind
    @pos = 0
    self
  end

  def <<(row)
    @string << CSV.generate_line(row, col_sep: @col_sep, quote_char: @quote_char,
                                 row_sep: @row_sep, force_quotes: @force_quotes)
    @rows = nil
    self
  end

  def add_row(row)
    self << row
  end

  def puts(row)
    self << row
  end
end
