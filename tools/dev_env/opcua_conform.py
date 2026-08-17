"""Check a companion-model module's address space against the NodeSets that publish it.

The modules under services/machine_tool build their node tree by hand, as a Browse switch of
add_obj / add_var / add_prop calls. What is easy to get wrong is not the shape but what each edge
DECLARES: the ReferenceType the parent reaches a child by, the child's NodeClass, and its
TypeDefinition. A client browsing for a HasProperty reference does not find a HasComponent one, so a
model that gets it wrong is unreadable by a conforming client without erroring anywhere.

    opcua_conform.py <module.c> <NodeSet2.xml> [more.xml ...]

A companion model builds on others and each names its own in <RequiredModel>; pass the whole chain.
Instance-of and subtype-of are followed (see uaspace.py), so a child declared on a container's type,
or on that type's base type in another NodeSet, still matches.

One line per edge the module publishes:

    OK       ref, type and class all agree with the published node
    REF      published under this parent, but with a different reference or TypeDefinition
    CLASS    published as a different NodeClass - an Object served as a Variable, say
    PARENT   the BrowseName is published, but never under a parent of this name
    ABSENT   no NodeSet given publishes this BrowseName at all

REF and CLASS are defects in the module. PARENT and ABSENT need a human: the module may be exposing
a name the specification does not have, or the chain may be short a NodeSet.
"""

import io
import os
import re
import sys

from uaspace import Space

# The add_* helpers as the modules spell them: an optional leading context, then the id and the
# BrowseName. A model that carries its namespace indices in its context passes that context first.
ADD = re.compile(
    r"\badd_(?P<kind>\w+)\s*\(\s*(?:[A-Za-z_]\w*\s*,\s*)?out\s*,[^,]+,[^,]+,\s*(?P<id>[^,]+),\s*\"(?P<name>[^\"]+)\""
)
CASE = re.compile(r"^\s*case\s+(?P<label>[A-Z_][A-Z0-9_]*)\s*:", re.M)

# What each helper declares. A type of None means the helper takes the TypeDefinition as an argument
# - a type the companion model owns rather than a core one - so only the reference and the NodeClass
# are checked here and the id is verified against the NodeSet by hand.
KIND = {
    "obj": ("UAObject", "HasComponent", "BaseObjectType"),
    "var": ("UAVariable", "HasComponent", "BaseDataVariableType"),
    "prop": ("UAVariable", "HasProperty", "PropertyType"),
    "folder": ("UAObject", "HasComponent", "FolderType"),
    "folder_member": ("UAObject", "Organizes", "BaseObjectType"),
    "addin": ("UAObject", "HasAddIn", None),
    "gt_obj": ("UAObject", "HasComponent", None),
    "typed_obj": ("UAObject", "HasComponent", None),
    "typed_var": ("UAVariable", "HasComponent", None),
}


def module_tree(path):
    """The (parent BrowseName, child BrowseName, kind) edges the Browse switch publishes.

    A case label is named by whichever add_* call elsewhere in the file targets it, so the tree
    reads in the module's own terms rather than in its enum spelling.
    """
    s = io.open(path, encoding="utf-8", errors="replace").read()
    label_of = {}
    for m in ADD.finditer(s):
        label_of.setdefault(m.group("id").strip(), m.group("name"))
    cases = [(m.start(), m.group("label")) for m in CASE.finditer(s)]
    edges = []
    for i, (at, label) in enumerate(cases):
        end = cases[i + 1][0] if i + 1 < len(cases) else len(s)
        parent = label_of.get(label, label)
        for m in ADD.finditer(s[at:end]):
            edges.append((parent, m.group("name"), m.group("kind")))
    return edges


def resolve_parent(space, parent):
    """The published nodes this parent names, and how that name was reached.

    A case label that no add_* call targets keeps its enum spelling (MOTIONDEVICE), because the tree
    reaches it through a computed id rather than by name. Those still have to be checked, so the
    label is normalized - underscores dropped, case folded, `Type` tried - and the match is reported
    as inferred so it is never mistaken for the module's own word.
    """
    hits = space.candidates(parent)
    if hits:
        return hits, ""
    flat = parent.replace("_", "").casefold()
    for name in space.by_name:
        if name.casefold() in (flat, flat + "type") or name.casefold().rstrip("type") == flat:
            return space.by_name[name], " [parent read as %s]" % name
    return [], ""


def verdict(space, edge):
    parent, child, kind = edge
    if kind not in KIND:
        return "HELPER", "add_%s is not in the helper table; state what it declares" % kind
    want_cls, want_ref, want_type = KIND[kind]

    parents, how = resolve_parent(space, parent)
    if not space.candidates(child) and not parents:
        return "ABSENT", ""

    seen_elsewhere, best = set(), None
    for p in parents:
        for ref, node in space.effective_children(p.key):
            if node.name != child:
                continue
            typedef = space.name_of(node.typedef)
            if node.cls != want_cls:
                return "CLASS", "published as %s, served as %s%s" % (node.cls, want_cls, how)
            if want_type is None:
                # The helper takes the type as an argument, so only the reference is mechanical.
                if ref == want_ref:
                    return "TYPED", "published %s -> %s; the id is the caller's%s" % (ref, typedef, how)
                best = "published %s, served %s%s" % (ref, want_ref, how)
                continue
            if ref == want_ref and typedef == want_type:
                return "OK", how.strip()
            best = "published %s -> %s, served %s -> %s%s" % (ref, typedef, want_ref, want_type, how)
    if best:
        return "REF", best

    for n in space.candidates(child):
        seen_elsewhere.add(parent_names(space, n))
    seen = sorted(x for x in seen_elsewhere if x)
    if seen:
        return "PARENT", "published under " + ", ".join(seen[:4])
    return "ABSENT", ""


def parent_names(space, node):
    """Whichever published node lists this one as a child - what a reader would call its parent."""
    for other in space.by_key.values():
        for _ref, child_key in other.children:
            if child_key == node.key:
                return other.name
    return ""


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    space = Space(argv[1:])
    counts = {}
    for e in module_tree(argv[0]):
        tag, why = verdict(space, e)
        counts[tag] = counts.get(tag, 0) + 1
        print("%-7s %s/%s%s" % (tag, e[0], e[1], ("   " + why) if why else ""))
    print("\n%s: %s" % (os.path.basename(argv[0]), ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
