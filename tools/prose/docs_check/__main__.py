#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The entry point. `python utils/maint/prose/docs_check` runs the whole package, because a directory
# holding this file is a script to the interpreter.
#
# The interpreter puts the directory itself on the path for that form and leaves __package__ unset,
# so a relative import has no parent to resolve against. Naming the package outright works under
# both that form and `python -m docs_check`, and needs the directory above this one on the path.

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# That same form also puts THIS directory at the front of the path, where every module beside this
# one is importable as a top level name and shadows any standard library module defined the same.
# locale.py shadows the real locale, which argparse reaches through gettext, and argparse then
# raises on a missing attribute instead of parsing. Take this directory back off: the line above
# already put the directory holding the package on, and that is the one an import needs.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [one for one in sys.path if one and os.path.abspath(one) != HERE]

from docs_check.run import main  # noqa: E402

raise SystemExit(main())
