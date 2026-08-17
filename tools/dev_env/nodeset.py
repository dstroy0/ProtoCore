"""Read an OPC UA NodeSet2 file and print what it publishes for each node.

The companion-model modules (services/machine_tool/*) build an address space by hand, and the thing
easiest to get wrong is not the node ids but the REFERENCE each parent reaches a child by:
HasProperty (46) to a PropertyType (68) node, or HasComponent (47) to a BaseDataVariableType (63)
one. A client browsing for one does not find the other. This answers it from the published document.

    nodeset.py <NodeSet2.xml>                 every UAObject and UAVariable
    nodeset.py <NodeSet2.xml> Name [Name ...] only those BrowseNames

Each line is: BrowseName, node class, the parent it hangs off, the reference that reaches it, and
its TypeDefinition. A reference reported as `?` means the child carries no inverse reference and the
NodeSet states the link on the parent instead - the TypeDefinition still discriminates, since
PropertyType and HasProperty always travel together.

The files are at https://github.com/OPCFoundation/UA-Nodeset - a companion model, plus whatever it
builds on (DI, Machinery), plus Schema/NodeIds.csv for the ns0 numbers.
"""

import re
import sys
import xml.etree.ElementTree as ET

NS = {"u": "http://opcfoundation.org/UA/2011/03/UANodeSet.xsd"}

# The ns0 ids these lines are read for, from the OPC Foundation's Schema/NodeIds.csv.
NAME = {
    "i=35": "Organizes",
    "i=46": "HasProperty",
    "i=47": "HasComponent",
    "i=58": "BaseObjectType",
    "i=63": "BaseDataVariableType",
    "i=68": "PropertyType",
}


def browse_name(node):
    """The BrowseName without its namespace index."""
    b = node.get("BrowseName", "")
    return b.split(":", 1)[1] if ":" in b else b


def namespaces(path):
    """The file's NamespaceUris, as the index a NodeId's ns= means inside THIS file.

    Index 0 is always the core OPC UA namespace and is not listed, so the table starts at 1.
    """
    root = ET.parse(path).getroot()
    uris = ["http://opcfoundation.org/UA/"]
    for u in root.findall("u:NamespaceUris/u:Uri", NS):
        uris.append((u.text or "").strip())
    return uris


def nodes_by_id(path):
    """(namespace uri, numeric id) -> BrowseName, for every node the file defines."""
    uris = namespaces(path)
    root = ET.parse(path).getroot()
    out = {}
    for n in root:
        nid = n.get("NodeId")
        if not nid:
            continue
        m = re.match(r"(?:ns=(\d+);)?i=(\d+)$", nid)
        if not m:
            continue
        idx = int(m.group(1) or 0)
        if idx < len(uris):
            out[(uris[idx], int(m.group(2)))] = browse_name(n)
    return out


def rows(path, want):
    root = ET.parse(path).getroot()
    by_id = {n.get("NodeId"): n for n in root if n.get("NodeId")}
    out = []
    for n in root:
        if n.tag.split("}")[-1] not in ("UAVariable", "UAObject"):
            continue
        name = browse_name(n)
        if want and name not in want:
            continue
        parent_ref, typedef = None, None
        for r in n.findall("u:References/u:Reference", NS):
            rt = r.get("ReferenceType", "")
            if rt in ("HasTypeDefinition", "i=40"):
                typedef = (r.text or "").strip()
            elif r.get("IsForward", "true") == "false":
                parent_ref = rt
        parent = n.get("ParentNodeId", "")
        out.append(
            (
                name,
                n.tag.split("}")[-1],
                browse_name(by_id[parent]) if parent in by_id else parent,
                NAME.get(parent_ref, parent_ref or "?"),
                NAME.get(typedef, typedef or "?"),
            )
        )
    return out


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    for name, cls, parent, ref, typedef in rows(argv[0], set(argv[1:])):
        print("%-26s %-12s parent=%-38s ref=%-16s typedef=%s" % (name, cls, parent, ref, typedef))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
