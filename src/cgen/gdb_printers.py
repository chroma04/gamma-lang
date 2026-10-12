# gdb pretty printers for Gamma values, embedded in executables built with `gamma build -g`.
#
# Values are shown through their debugger views (src/cgen/views.rs). A view whose
# struct tag ends in `_slice`, `_string`, `_optional`, `_result` or `_union` is shown
# the Gamma way: a slice as its elements, a string as text, an optional as its value or
# `null`, a result as its value or `!error`, and a tagged union as its variant.
import re

import gdb


def _members(v):
    """The named members of struct view `v`, including those of its anonymous union."""
    out = {}
    for f in v.type.strip_typedefs().fields():
        if f.name:
            out[f.name] = v[f.name]
        elif not f.is_base_class and f.type is not None and f.type.strip_typedefs().code == gdb.TYPE_CODE_UNION:
            for g in f.type.strip_typedefs().fields():
                if g.name:
                    out[g.name] = v[f][g.name]
    return out


class SlicePrinter:
    def __init__(self, v):
        self.ptr = v["ptr"]
        self.len = int(v["len"])

    def to_string(self):
        return "length %d" % self.len

    def children(self):
        if self.ptr.type.target().strip_typedefs().code == gdb.TYPE_CODE_VOID:
            return
        limit = min(self.len, 10000)
        for i in range(limit):
            yield "[%d]" % i, self.ptr[i]

    def display_hint(self):
        return "array"


class StringPrinter:
    def __init__(self, v):
        self.ptr = v["ptr"]
        self.len = int(v["len"])

    def to_string(self):
        return self.ptr.cast(gdb.lookup_type("char").pointer()).lazy_string(length=self.len)

    def display_hint(self):
        return "string"


class OptionalPrinter:
    def __init__(self, v):
        self.v = v

    def to_string(self):
        if self.v["present"]:
            return _members(self.v).get("value", "()")
        return "null"


class ResultPrinter:
    def __init__(self, v):
        self.v = v

    def to_string(self):
        m = _members(self.v)
        if self.v["is_err"]:
            return "!" + str(m["err"]) if "err" in m else "!"
        return m["ok"] if "ok" in m else "()"


class UnionPrinter:
    def __init__(self, v):
        self.v = v
        self.m = _members(v)
        self.tag = str(v["tag"])
        self.payload = None
        for name, value in self.m.items():
            if name != "tag" and self.tag.endswith("_" + name):
                self.payload = value

    def to_string(self):
        if self.payload is None:
            return self.tag
        t = self.payload.type.strip_typedefs()
        if t.code in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_ARRAY):
            return self.tag
        return "%s(%s)" % (self.tag, self.payload)

    def children(self):
        if self.payload is None:
            return
        t = self.payload.type.strip_typedefs()
        if t.code == gdb.TYPE_CODE_STRUCT:
            for name, value in _members(self.payload).items():
                yield name, value
        elif t.code == gdb.TYPE_CODE_ARRAY:
            yield "payload", self.payload


_KINDS = {
    "slice": SlicePrinter,
    "string": StringPrinter,
    "optional": OptionalPrinter,
    "result": ResultPrinter,
    "union": UnionPrinter,
}
_VIEW = re.compile(r"^gm_dv\d+_(\w+)$")


def _lookup(v):
    t = v.type.strip_typedefs()
    if t.code != gdb.TYPE_CODE_STRUCT or t.tag is None:
        return None
    m = _VIEW.match(t.tag)
    if m is None or m.group(1) not in _KINDS:
        return None
    return _KINDS[m.group(1)](v)


_objfile = gdb.current_objfile()
(_objfile.pretty_printers if _objfile is not None else gdb.pretty_printers).append(_lookup)
