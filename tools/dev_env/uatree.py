"""Print the address space a set of NodeSet2 files publishes, as a tree.

    uatree.py <root BrowseName> <depth> <NodeSet2.xml> [more.xml ...]

Instance-of and subtype-of are followed, so what prints is what a client browsing that node actually
sees, not just what the one file spelled out under it. Each line is the reference, the BrowseName,
the NodeClass and the TypeDefinition - the four things a ReferenceDescription carries and the four a
hand-built model has to match.

Use it to answer "what does the specification actually call this" before changing a model: the
containers are usually plural folders holding typed instances (Channels holding
ChannelMonitoringType), which is where a hand-built singular `Channel` diverges.
"""

import sys

from uaspace import Space


def walk(space, node, depth, indent, seen):
    if depth <= 0 or node.key in seen:
        return
    seen = seen | {node.key}
    for ref, child in space.effective_children(node.key):
        print("%s%-14s %-30s %-11s %s" % (indent, ref, child.name, child.cls, space.name_of(child.typedef)))
        walk(space, child, depth - 1, indent + "  ", seen)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    root, depth, space = argv[0], int(argv[1]), Space(argv[2:])
    hits = space.candidates(root)
    if not hits:
        print("no node published under that BrowseName")
        return 1
    for n in hits:
        print("== %s (%s, type %s) ==" % (n.name, n.cls, space.name_of(n.typedef)))
        walk(space, n, depth, "  ", set())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
