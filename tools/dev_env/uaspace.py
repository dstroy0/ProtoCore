"""The address space a set of NodeSet2 files describes, with instance-of and subtype-of followed.

A NodeSet declares a container's children on its TYPE, not on each instance, and a type inherits its
base type's children. So the children a client actually browses under a node are:

    direct children of the node
  + the children of its TypeDefinition
  + the children of every supertype of that TypeDefinition

Reading the file without following those two links makes almost every real edge look missing, which
is the trap: `Identification` in MachineTool declares five of its leaves itself and inherits
`YearOfConstruction` from `MachineryItemIdentificationType` in another NodeSet entirely.

Nodes are keyed by (namespace uri, numeric id), so ids stay distinct across models and a `ns=2;i=1024`
written in one file resolves to the model that owns it.
"""

import re
import xml.etree.ElementTree as ET

NS = {"u": "http://opcfoundation.org/UA/2011/03/UANodeSet.xsd"}

HAS_SUBTYPE = "i=45"
HAS_TYPEDEF = "i=40"
NODEID = re.compile(r"(?:ns=(\d+);)?i=(\d+)$")

# The ns0 reference ids that put a child under a parent, under the names the specs use. HasAddIn
# (Part 3) is how DI, Machinery and MachineTool attach Identification and the other add-ins, and
# leaving it out hides a whole branch of every model built on those.
REF_NAME = {
    "i=35": "Organizes",
    "i=46": "HasProperty",
    "i=47": "HasComponent",
    "i=17604": "HasAddIn",
}
HIERARCHICAL = set(REF_NAME) | set(REF_NAME.values())


class Node:
    __slots__ = ("key", "name", "cls", "typedef", "supertype", "children")

    def __init__(self, key, name, cls):
        self.key = key
        self.name = name
        self.cls = cls
        self.typedef = None   # key of its TypeDefinition
        self.supertype = None # key of the type it is a subtype of
        self.children = []    # (ref name, child key)


def _bn(node):
    b = node.get("BrowseName", "")
    return b.split(":", 1)[1] if ":" in b else b


class Space:
    def __init__(self, paths):
        self.by_key = {}
        self.by_name = {}
        for p in paths:
            self._load(p)
        for n in self.by_key.values():
            self.by_name.setdefault(n.name, []).append(n)

    def _uris(self, path):
        root = ET.parse(path).getroot()
        uris = ["http://opcfoundation.org/UA/"]
        for u in root.findall("u:NamespaceUris/u:Uri", NS):
            uris.append((u.text or "").strip())
        return uris

    def _key(self, uris, nid):
        m = NODEID.match((nid or "").strip())
        if not m:
            return None
        idx = int(m.group(1) or 0)
        return (uris[idx] if idx < len(uris) else "?%d" % idx, int(m.group(2)))

    def _load(self, path):
        uris = self._uris(path)
        root = ET.parse(path).getroot()
        for e in root:
            tag = e.tag.split("}")[-1]
            if not tag.startswith("UA") or tag in ("UAModelChangeEvent",):
                continue
            key = self._key(uris, e.get("NodeId"))
            if key is None:
                continue
            node = self.by_key.get(key) or Node(key, _bn(e), tag)
            node.name, node.cls = _bn(e), tag
            self.by_key[key] = node
            for r in e.findall("u:References/u:Reference", NS):
                rt = (r.get("ReferenceType") or "").strip()
                target = self._key(uris, (r.text or "").strip())
                if target is None:
                    continue
                forward = r.get("IsForward", "true") != "false"
                if rt in (HAS_TYPEDEF, "HasTypeDefinition"):
                    node.typedef = target
                elif rt in (HAS_SUBTYPE, "HasSubtype") and not forward:
                    node.supertype = target
                elif forward and rt in HIERARCHICAL:
                    node.children.append((REF_NAME.get(rt, rt), target))

    def name_of(self, key):
        n = self.by_key.get(key)
        return n.name if n else ("%s:%d" % key if key else "?")

    def effective_children(self, key, seen=None):
        """Every (ref, child node) a client browses under this node: its own, its type's, and its
        type's supertypes'."""
        if seen is None:
            seen = set()
        if key is None or key in seen:
            return []
        seen.add(key)
        node = self.by_key.get(key)
        if node is None:
            return []
        out, taken = [], set()
        for ref, child_key in node.children:
            child = self.by_key.get(child_key)
            if child is not None:
                out.append((ref, child))
        out += self.effective_children(node.typedef, seen)
        out += self.effective_children(node.supertype, seen)
        # A type and its base can both list a child, and an instance can restate one its type
        # declares. One edge is one edge.
        uniq = []
        for ref, child in out:
            mark = (ref, child.name, child.key)
            if mark not in taken:
                taken.add(mark)
                uniq.append((ref, child))
        return uniq

    def candidates(self, name):
        """Every node published under this BrowseName."""
        return self.by_name.get(name, [])
