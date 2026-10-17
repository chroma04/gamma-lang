# LLDB data formatters for Gamma values, written next to executables built with
# `gamma build -g` as `gamma_lldb.py`. Load them with
#
#     lldb -o 'command script import gamma_lldb.py' ./hello
#
# Values are shown through their debugger views (src/cgen/views.rs). A view whose struct
# tag is `gm_dv<N>_<kind>` is shown the Gamma way: a slice as its elements, a string as
# text, an optional as its value or `null`, a result as its value or `!error`, and a
# tagged union as its variant. These are the conventions of the gdb pretty printers.
import lldb

MAX_ELEMENTS = 10000
MAX_STRING = 64 * 1024


def _plain(v):
    """`v` without its synthetic children, so its real members can be looked up."""
    nv = v.GetNonSyntheticValue()
    return nv if nv.IsValid() else v


def _members(v):
    """The named members of struct view `v` (in order), looking through anonymous unions."""
    out = {}
    v = _plain(v)
    for i in range(v.GetNumChildren()):
        c = v.GetChildAtIndex(i)
        name = c.GetName()
        if name:
            out[name] = c
        else:
            for j in range(c.GetNumChildren()):
                g = c.GetChildAtIndex(j)
                if g.GetName():
                    out[g.GetName()] = g
    return out


def _class(v):
    return _plain(v).GetType().GetCanonicalType().GetTypeClass()


def _is_aggregate(v):
    return _class(v) in (lldb.eTypeClassStruct, lldb.eTypeClassClass, lldb.eTypeClassUnion, lldb.eTypeClassArray)


def _show(v):
    """`v` as a one-line string: its summary or value."""
    s = v.GetSummary()
    if s is None:
        s = v.GetValue()
    if s is None:
        # A struct without a summary: show its members inline.
        parts = []
        for i in range(v.GetNumChildren()):
            c = v.GetChildAtIndex(i)
            parts.append("%s = %s" % (c.GetName(), _show(c)))
        s = "{" + ", ".join(parts) + "}"
    return s


def _flag(v):
    return v.GetValueAsUnsigned() != 0


# ---- slices -----------------------------------------------------------------------------


class SliceProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def update(self):
        m = _members(self.valobj)
        self.ptr = m.get("ptr")
        self.len = m["len"].GetValueAsUnsigned() if "len" in m else 0
        self.elem = None
        self.size = 0
        if self.ptr is not None:
            self.elem = self.ptr.GetType().GetPointeeType()
            if self.elem.GetCanonicalType().GetBasicType() == lldb.eBasicTypeVoid:
                self.elem = None
            else:
                self.size = self.elem.GetByteSize()
        return False

    def has_children(self):
        return self.elem is not None and self.len > 0

    def num_children(self):
        return min(self.len, MAX_ELEMENTS) if self.elem is not None else 0

    def get_child_index(self, name):
        try:
            return int(name.strip("[]"))
        except ValueError:
            return -1

    def get_child_at_index(self, i):
        if self.elem is None or i < 0 or i >= self.num_children():
            return None
        addr = self.ptr.GetValueAsUnsigned() + i * self.size
        return self.ptr.CreateValueFromAddress("[%d]" % i, addr, self.elem)


def slice_summary(valobj, internal_dict):
    m = _members(valobj)
    return "length %d" % (m["len"].GetValueAsUnsigned() if "len" in m else 0)


# ---- strings ----------------------------------------------------------------------------


def string_summary(valobj, internal_dict):
    m = _members(valobj)
    n = m["len"].GetValueAsUnsigned() if "len" in m else 0
    addr = m["ptr"].GetValueAsUnsigned() if "ptr" in m else 0
    text = b""
    if n and addr:
        err = lldb.SBError()
        text = valobj.GetProcess().ReadMemory(addr, min(n, MAX_STRING), err)
        if not err.Success():
            return "<unreadable>"
    s = text.decode("utf-8", errors="replace")
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t").replace("\r", "\\r")
    return '"%s%s"' % (s, "..." if n > MAX_STRING else "")


# ---- children that are the members of a payload -------------------------------------------


class _Payload:
    """Synthetic children that are the members of a payload value, if it is a struct."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.kids = []

    def _payload(self):
        return None

    def update(self):
        p = self._payload()
        self.kids = []
        if p is not None and _is_aggregate(p):
            if _class(p) == lldb.eTypeClassArray:
                self.kids = [("payload", p)]
            else:
                self.kids = list(_members(p).items())
        return False

    def has_children(self):
        return bool(self.kids)

    def num_children(self):
        return len(self.kids)

    def get_child_index(self, name):
        for i, (n, _) in enumerate(self.kids):
            if n == name:
                return i
        return -1

    def get_child_at_index(self, i):
        if 0 <= i < len(self.kids):
            n, c = self.kids[i]
            return c.Clone(n)
        return None


# ---- optionals --------------------------------------------------------------------------


class OptionalProvider(_Payload):
    def _payload(self):
        m = _members(self.valobj)
        if "present" in m and _flag(m["present"]):
            return m.get("value")
        return None


def optional_summary(valobj, internal_dict):
    m = _members(valobj)
    if "present" in m and _flag(m["present"]):
        v = m.get("value")
        if v is None:
            return "()"
        return "" if _is_aggregate(v) and v.GetSummary() is None else _show(v)
    return "null"


# ---- results ----------------------------------------------------------------------------


def result_summary(valobj, internal_dict):
    m = _members(valobj)
    if "is_err" in m and _flag(m["is_err"]):
        return "!" + (_show(m["err"]) if "err" in m else "")
    return _show(m["ok"]) if "ok" in m else "()"


# ---- tagged unions ----------------------------------------------------------------------


def _union_parts(valobj):
    """The tag's name and the active payload member (or None) of a union view."""
    m = _members(valobj)
    tag = m.get("tag")
    if tag is None:
        return "", None
    name = tag.GetValue() or str(tag.GetValueAsUnsigned())
    payload = None
    for k, v in m.items():
        if k != "tag" and name.endswith("_" + k):
            payload = v
    return name, payload


class UnionProvider(_Payload):
    def _payload(self):
        return _union_parts(self.valobj)[1]


def union_summary(valobj, internal_dict):
    tag, payload = _union_parts(valobj)
    if payload is None or _is_aggregate(payload):
        return tag
    return "%s(%s)" % (tag, _show(payload))


# ---- registration -----------------------------------------------------------------------

_KINDS = {
    "slice": SliceProvider,
    "string": None,
    "optional": OptionalProvider,
    "result": None,
    "union": UnionProvider,
}


def __lldb_init_module(debugger, internal_dict):
    for kind, provider in _KINDS.items():
        regex = "^gm_dv[0-9]+_%s$" % kind
        # `-e` shows the children (the synthetic ones) below the summary.
        expand = "-e " if provider is not None else ""
        debugger.HandleCommand(
            "type summary add %s-x '%s' --category gamma --python-function %s.%s_summary"
            % (expand, regex, __name__, kind)
        )
        if provider is not None:
            debugger.HandleCommand(
                "type synthetic add -x '%s' --category gamma --python-class %s.%s"
                % (regex, __name__, provider.__name__)
            )
    debugger.HandleCommand("type category enable gamma")
