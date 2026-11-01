"""Turn the scalar arrays in a generated C file into exact m68k ROM bytes.

Generated data files (CHR, rooms, PCM, music ...) are plain initialised arrays
of integer scalars. parse() evaluates each one as GCC lays it out for m68k:
big-endian elements, row-major, missing elements zero, alignment from an
explicit __attribute__((aligned(N))) or else 2. residue() rewrites the file
with each array definition replaced by an extern declaration of the same
type and dimensions (static dropped so the .nesdata piece symbol is global),
leaving every other line (size constants, pointer tables, structs) intact.
Definitions this parser cannot evaluate are left untouched in the residue.

T-419. Spec: docs/superpowers/specs/2026-10-31-T419-one-file-rom-maker-design.md
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

TYPE_SIZES = {
    "unsigned char": 1, "signed char": 1, "char": 1, "u8": 1, "s8": 1, "uint8_t": 1, "int8_t": 1,
    "unsigned short": 2, "short": 2, "signed short": 2, "u16": 2, "s16": 2, "uint16_t": 2,
    "int16_t": 2, "unsigned long": 4, "long": 4, "signed long": 4, "unsigned int": 4, "int": 4,
    "u32": 4, "s32": 4, "uint32_t": 4, "int32_t": 4,
}
_TYPES = "|".join(sorted((re.escape(t).replace(r"\ ", r"\s+") for t in TYPE_SIZES), key=len, reverse=True))
_ATTR = r"__attribute__\s*\(\(\s*aligned\s*\(\s*(\d+)\s*\)\s*\)\)"
DEF = re.compile(
    r"^(?P<static>static\s+)?const\s+(?P<type>" + _TYPES + r"|struct\s+[A-Za-z_]\w*)\s+"
    r"(?:" + _ATTR.replace("(\\d+)", "(?P<a1>\\d+)") + r"\s+)?"
    r"(?P<name>[A-Za-z_]\w*)\s*(?P<dims>(?:\[[^\]]*\]\s*)+)"
    r"(?:" + _ATTR.replace("(\\d+)", "(?P<a2>\\d+)") + r"\s*)?=\s*\{", re.M)
INT = re.compile(r"^(-?)\s*(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*$")
STRUCT_DEF = re.compile(r"\bstruct\s+([A-Za-z_]\w*)\s*\{([^{}]*)\}\s*;")
FIELD = re.compile(r"^\s*(" + _TYPES + r")\s+(.+)$")

# A struct layout: (element size, element count) per field, declaration order.
Fields = list[tuple[int, int]]


def structs_in(text: str) -> dict[str, Fields]:
    """Struct definitions whose fields are all integer scalars or arrays of them."""
    found: dict[str, Fields] = {}
    for m in STRUCT_DEF.finditer(_blank_comments(text)):
        fields: Fields = []
        try:
            for decl in filter(str.strip, m.group(2).split(";")):
                f = FIELD.match(decl)
                if not f:
                    raise ValueError(decl)
                size = TYPE_SIZES[re.sub(r"\s+", " ", f.group(1))]
                for name in f.group(2).split(","):
                    dims = re.findall(r"\[\s*(\w+)\s*\]", name)
                    count = 1
                    for d in dims:
                        count *= _value(d)
                    fields.append((size, count))
        except (ValueError, KeyError):
            continue
        found[m.group(1)] = fields
    return found


def _struct_layout(fields: Fields) -> tuple[list[int], int]:
    """Field offsets and struct size under m68k GCC rules (measured: field align
    min(size, 2); size rounded up to the largest field alignment)."""
    offsets, pos, align = [], 0, 1
    for size, count in fields:
        a = min(size, 2)
        pos = (pos + a - 1) // a * a
        offsets.append(pos)
        pos += size * count
        align = max(align, a)
    return offsets, (pos + align - 1) // align * align


def _struct_bytes(fields: Fields, init: object) -> bytes:
    values = init if isinstance(init, list) else [init]
    if len(values) > len(fields):
        raise ValueError("too many struct initializers")
    offsets, total = _struct_layout(fields)
    out = bytearray(total)
    for (size, count), off, value in zip(fields, offsets, values):
        flat = _flatten(value, [count]) if count > 1 or isinstance(value, list) else [value]
        for k, v in enumerate(flat):
            out[off + k * size:off + (k + 1) * size] = (v & ((1 << (8 * size)) - 1)).to_bytes(size, "big")
    return bytes(out)


@dataclass(frozen=True)
class Array:
    name: str
    ctype: str
    elem_size: int
    dims: tuple[int, ...]
    align: int
    static: bool
    data: bytes
    span: tuple[int, int]


@dataclass(frozen=True)
class Parsed:
    arrays: tuple[Array, ...]
    skipped: tuple[str, ...]


_LITERAL_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?(?:\*/|\Z)|//[^\n]*', re.S)


def _blank_comments(text: str) -> str:
    """Same length as text, comments replaced by spaces (string/char literals kept).
    Regex-driven: the builder page runs this over ~10 MB of generated C in Pyodide."""
    def blank(m: re.Match) -> str:
        s = m.group(0)
        if s[0] in "\"'":
            return s
        return re.sub(r"[^\n]", " ", s)
    return _LITERAL_OR_COMMENT.sub(blank, text)


def _value(token: str) -> int:
    token = token.strip()
    if len(token) >= 3 and token[0] == token[-1] == "'":
        body = token[1:-1]
        escapes = {"\\0": 0, "\\n": 10, "\\t": 9, "\\r": 13, "\\\\": 92, "\\'": 39, '\\"': 34}
        if body in escapes:
            return escapes[body]
        if len(body) == 1:
            return ord(body)
        raise ValueError(token)
    m = INT.match(token)
    if not m:
        raise ValueError(token)
    v = int(m.group(2), 0) if not re.fullmatch(r"0\d+", m.group(2)) else int(m.group(2), 8)
    return -v if m.group(1) else v


@dataclass(frozen=True)
class _At:
    """A designated element `[index] = value` inside a brace list."""
    index: int
    value: object


# Initializer tokens: braces, commas, designators `[n] =`, char literals, other text.
_TOKEN = re.compile(r"\{|\}|,|\[[^\]]*\]\s*=|'(?:\\.|[^'\\])'|[^{},\['\s][^{},\[']*|\s+")
_FLAT = re.compile(r"\{([^{}\[']*)\}")
_ALL_NUMBERS = re.compile(r"(?<![\w.])(-?)\s*(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*(?![\w.])")


def _int_literal(sign: str, digits: str) -> int:
    if digits[:2] in ("0x", "0X"):
        v = int(digits, 16)
    elif len(digits) > 1 and digits[0] == "0":
        v = int(digits, 8)
    else:
        v = int(digits)
    return -v if sign else v


def _initializer(text: str, start: int) -> tuple[object, int]:
    """Parse a brace initializer starting at text[start] == '{' -> (nested lists, end index).
    Regex-tokenised (not char-by-char) so ~10 MB of generated C stays fast under Pyodide."""
    flat = _flat_list(text, start)
    if flat:  # fast path: one flat list of numbers (most generated arrays)
        return flat
    stack: list[list] = []
    designators: list[int | None] = []
    pending: str | None = None

    def put(item: object) -> None:
        index = designators[-1]
        designators[-1] = None
        stack[-1].append(_At(index, item) if index is not None else item)

    pos = start
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if m is None:
            raise ValueError(f"unexpected text at {pos}")
        tok, pos = m.group(0), m.end()
        if tok == "{":
            inner = _flat_list(text, m.start()) if stack else None
            if inner:  # innermost row: parse it in one go
                put(inner[0])
                pos = inner[1]
                continue
            stack.append([])
            designators.append(None)
        elif tok in ",}":
            if pending is not None and pending.strip():
                put(_value(pending))
            pending = None
            if tok == "}":
                done = stack.pop()
                designators.pop()
                if not stack:
                    return done, pos
                put(done)
        elif tok[0] == "[":
            designators[-1] = _value(tok[1:tok.index("]")])
        elif not tok.isspace():
            pending = tok if pending is None else pending + tok
    raise ValueError("unterminated initializer")


def _flat_list(text: str, start: int) -> tuple[list[int], int] | None:
    """`{n, n, ...}` (no nesting, designators or char literals) at text[start], or None."""
    flat = _FLAT.match(text, start)
    if not flat:
        return None
    body = flat.group(1)
    items = [s for s in body.split(",") if s.strip()]
    nums = _ALL_NUMBERS.findall(body)
    if len(nums) == len(items):  # every item is a plain integer literal
        return [_int_literal(sign, digits) for sign, digits in nums], flat.end()
    return [_value(s) for s in items], flat.end()


def _dims(spec: str, macros: dict[str, int]) -> list[int | None]:
    out: list[int | None] = []
    for d in re.findall(r"\[([^\]]*)\]", spec):
        d = d.strip()
        if not d:
            out.append(None)
        elif d in macros:
            out.append(macros[d])
        else:
            out.append(_value(d))
    return out


def _flatten(values: object, dims: list[int], depth: int = 0) -> list[int]:
    count = dims[depth]
    items = values if isinstance(values, list) else [values]
    if any(isinstance(v, _At) for v in items):
        inner = 1
        for d in dims[depth + 1:]:
            inner *= d
        out = [0] * (count * inner)
        cursor = 0
        for v in items:
            if isinstance(v, _At):
                cursor, v = v.index, v.value
            if cursor >= count:
                raise ValueError("designator out of range")
            row = _flatten(v, dims, depth + 1) if depth < len(dims) - 1 else [v]
            out[cursor * inner:(cursor + 1) * inner] = row
            cursor += 1
        return out
    if depth == len(dims) - 1:
        flat: list[int] = []
        for v in items:
            if isinstance(v, list):  # braces around a scalar
                flat.extend(_flatten(v, dims, depth))
            else:
                flat.append(v)
        if len(flat) > count:
            raise ValueError("too many initializers")
        return flat + [0] * (count - len(flat))
    inner = 1
    for d in dims[depth + 1:]:
        inner *= d
    flat = []
    if items and not isinstance(items[0], list):  # flat initializer for a multi-dim array
        flat = list(items)
        if len(flat) > count * inner:
            raise ValueError("too many initializers")
        return flat + [0] * (count * inner - len(flat))
    if len(items) > count:
        raise ValueError("too many initializers")
    for row in items:
        flat.extend(_flatten(row, dims, depth + 1))
    return flat + [0] * ((count - len(items)) * inner)


def parse(text: str, macros: dict[str, int], structs: dict[str, Fields] | None = None) -> Parsed:
    clean = _blank_comments(text)
    structs = structs or {}
    arrays: list[Array] = []
    skipped: list[str] = []
    for m in DEF.finditer(clean):
        brace = m.end() - 1
        ctype = re.sub(r"\s+", " ", m.group("type"))
        try:
            values, end = _initializer(clean, brace)
            semi = clean.index(";", end)
            if clean[end:semi].strip():
                raise ValueError("trailing tokens")
            dims = _dims(m.group("dims"), macros)
            if None in dims[1:]:
                raise ValueError("only the first dimension may be empty")
            if dims[0] is None:
                if not isinstance(values, list):
                    raise ValueError("scalar initializer")
                dims[0] = len(values)
            if ctype.startswith("struct "):
                fields = structs[ctype.split()[1]]
                if len(dims) != 1 or not isinstance(values, list) or len(values) > dims[0]:
                    raise ValueError("only 1-D struct arrays")
                size = _struct_layout(fields)[1]
                data = b"".join(_struct_bytes(fields, v) for v in values) + bytes(size * (dims[0] - len(values)))
            else:
                flat = _flatten(values, dims)  # type: ignore[arg-type]
                size = TYPE_SIZES[ctype]
                mask = (1 << (8 * size)) - 1
                data = struct.pack(f">{len(flat)}{'BHI'[size // 2]}", *(v & mask for v in flat))
        except (ValueError, IndexError, KeyError):
            skipped.append(m.group("name"))
            continue
        align = int(m.group("a1") or m.group("a2") or 2)
        arrays.append(Array(name=m.group("name"), ctype=ctype, elem_size=size, dims=tuple(dims),  # type: ignore[arg-type]
                            align=align, static=bool(m.group("static")), data=data,
                            span=(m.start(), semi + 1)))
    return Parsed(arrays=tuple(arrays), skipped=tuple(skipped))


def declaration(a: Array) -> str:
    dims = "".join(f"[{d}]" for d in a.dims)
    return f"extern const {a.ctype} {a.name}{dims};"


def residue(text: str, parsed: Parsed) -> str:
    out = []
    pos = 0
    for a in sorted(parsed.arrays, key=lambda a: a.span[0]):
        out.append(text[pos:a.span[0]])
        out.append(declaration(a))
        pos = a.span[1]
    out.append(text[pos:])
    return "".join(out)
