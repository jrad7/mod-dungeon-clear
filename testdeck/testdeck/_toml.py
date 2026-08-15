"""Minimal TOML reader — the fallback when neither tomllib nor tomli exists.

Python grew `tomllib` in 3.11. AzerothCore's most common host is still Ubuntu
22.04 (Python 3.10), and asking every server admin to `pip install tomli`
before the dashboard will start is a worse first-run experience than parsing
the small config dialect ourselves.

`loads()` handles exactly what ac-dashboard.toml is allowed to contain:

    # comments, whole-line or trailing
    key = "string"          basic strings with \\ escapes
    key = 'literal'         literal strings, no escapes
    key = 12  -3  0         integers (underscores allowed)
    key = 2.5  -0.5  1e3    floats (the collector cadences are fractional)
    key = true | false      booleans
    key = ["a", "b"]        arrays, may span lines, trailing comma ok
    [table]  [a.b.c]        tables, dotted names
    [[array.of.tables]]     arrays of tables

Deliberately NOT supported: datetimes, multi-line strings, inline tables,
dotted keys, hex/octal ints. Anything unsupported raises TomlError
naming the line, so a user writing valid-but-unhandled TOML gets a clear
message instead of a wrong value. When tomllib/tomli IS available the loader
in config.py prefers it, so those users get the full grammar.
"""

import re

__all__ = ["loads", "TomlError"]


class TomlError(ValueError):
    pass


_KEY = r"[A-Za-z0-9_-]+"
_TABLE_RE = re.compile(rf"^\[\s*({_KEY}(?:\s*\.\s*{_KEY})*)\s*\]$")
_ARRAY_TABLE_RE = re.compile(rf"^\[\[\s*({_KEY}(?:\s*\.\s*{_KEY})*)\s*\]\]$")
_ASSIGN_RE = re.compile(rf"^({_KEY})\s*=\s*(.*)$")
_INT_RE = re.compile(r"^[+-]?[0-9](?:[0-9_]*[0-9])?$")
_FLOAT_RE = re.compile(r"^[+-]?[0-9](?:[0-9_]*[0-9])?(\.[0-9](?:[0-9_]*[0-9])?)?([eE][+-]?[0-9]+)?$")

_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\",
            "b": "\b", "f": "\f", "0": "\0"}


def loads(text):
    """Parse TOML source into a dict. Raises TomlError with a line number."""
    root = {}
    # The table the next bare `key = value` belongs to.
    cur = root
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        lineno = i + 1
        i += 1
        line = _strip_comment(raw).strip()
        if not line:
            continue

        m = _ARRAY_TABLE_RE.match(line)
        if m:
            cur = _push_array_table(root, _split_key(m.group(1)), lineno)
            continue
        m = _TABLE_RE.match(line)
        if m:
            cur = _descend(root, _split_key(m.group(1)), lineno)
            continue
        m = _ASSIGN_RE.match(line)
        if not m:
            raise TomlError(f"line {lineno}: cannot parse {raw.strip()!r}")
        key, rest = m.group(1), m.group(2)

        # An array is the one value that may run past the end of its line.
        if rest.lstrip().startswith("["):
            rest, i = _gather_array(rest, lines, i, lineno)
        if key in cur:
            raise TomlError(f"line {lineno}: duplicate key {key!r}")
        cur[key] = _value(rest.strip(), lineno)
    return root


def _strip_comment(line):
    """Drop a trailing `# comment`, honouring quotes so a '#' inside a string
    survives (paths and passwords legitimately contain one)."""
    out, quote, esc = [], None, False
    for ch in line:
        if esc:
            out.append(ch)
            esc = False
            continue
        if quote:
            if ch == "\\" and quote == '"':
                esc = True
            elif ch == quote:
                quote = None
            out.append(ch)
            continue
        if ch in "\"'":
            quote = ch
            out.append(ch)
            continue
        if ch == "#":
            break
        out.append(ch)
    return "".join(out)


def _split_key(name):
    return [p.strip() for p in name.split(".")]


def _descend(root, parts, lineno):
    node = root
    for p in parts:
        nxt = node.setdefault(p, {})
        if isinstance(nxt, list):       # [[a]] then [a.b] targets the last one
            nxt = nxt[-1]
        if not isinstance(nxt, dict):
            raise TomlError(f"line {lineno}: {p!r} is already a value, not a table")
        node = nxt
    return node


def _push_array_table(root, parts, lineno):
    parent = _descend(root, parts[:-1], lineno) if len(parts) > 1 else root
    leaf = parts[-1]
    arr = parent.setdefault(leaf, [])
    if not isinstance(arr, list):
        raise TomlError(f"line {lineno}: {leaf!r} is already a table, not an array")
    entry = {}
    arr.append(entry)
    return entry


def _gather_array(rest, lines, i, lineno):
    """Join continuation lines until the array's brackets balance."""
    depth = _depth(rest)
    while depth > 0:
        if i >= len(lines):
            raise TomlError(f"line {lineno}: unterminated array")
        nxt = _strip_comment(lines[i])
        i += 1
        rest += " " + nxt.strip()
        depth += _depth(nxt)
    return rest, i


def _depth(s):
    d, quote, esc = 0, None, False
    for ch in s:
        if esc:
            esc = False
            continue
        if quote:
            if ch == "\\" and quote == '"':
                esc = True
            elif ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
        elif ch == "[":
            d += 1
        elif ch == "]":
            d -= 1
    return d


def _value(s, lineno):
    if not s:
        raise TomlError(f"line {lineno}: missing value")
    if s[0] == "[":
        return [_value(item, lineno) for item in _split_items(s[1:-1], lineno)]
    if s[0] == '"':
        return _basic_string(s, lineno)
    if s[0] == "'":
        if len(s) < 2 or s[-1] != "'":
            raise TomlError(f"line {lineno}: unterminated literal string")
        return s[1:-1]
    if s in ("true", "false"):
        return s == "true"
    if _INT_RE.match(s):
        return int(s.replace("_", ""))
    if _FLOAT_RE.match(s):
        return float(s.replace("_", ""))
    raise TomlError(f"line {lineno}: unsupported value {s!r} "
                    "(this reader handles strings, numbers, booleans, "
                    "arrays and tables — install tomli for the full grammar)")


def _split_items(body, lineno):
    """Split an array body on top-level commas."""
    items, buf, depth, quote, esc = [], [], 0, None, False
    for ch in body:
        if esc:
            buf.append(ch)
            esc = False
            continue
        if quote:
            if ch == "\\" and quote == '"':
                esc = True
            elif ch == quote:
                quote = None
            buf.append(ch)
            continue
        if ch in "\"'":
            quote = ch
        elif ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        elif ch == "," and depth == 0:
            items.append("".join(buf).strip())
            buf = []
            continue
        buf.append(ch)
    if quote:
        raise TomlError(f"line {lineno}: unterminated string in array")
    tail = "".join(buf).strip()
    if tail:
        items.append(tail)
    return items


def _basic_string(s, lineno):
    if len(s) < 2 or s[-1] != '"':
        raise TomlError(f"line {lineno}: unterminated string")
    out, esc = [], False
    for ch in s[1:-1]:
        if esc:
            if ch not in _ESCAPES:
                raise TomlError(f"line {lineno}: unknown escape \\{ch}")
            out.append(_ESCAPES[ch])
            esc = False
        elif ch == "\\":
            esc = True
        else:
            out.append(ch)
    if esc:
        raise TomlError(f"line {lineno}: string ends in a backslash")
    return "".join(out)
