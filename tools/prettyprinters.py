from __future__ import print_function

import gdb.printing

class StrongIntegerPrinter:
    """Print an strongly typed integer"""

    def __init__(self, val):
        self.val = val.cast(gdb.lookup_type("long"))

    def to_string(self):
        return str(self.val)


pp = gdb.printing.RegexpCollectionPrettyPrinter("LoopModels")


gdb.printing.register_pretty_printer(gdb.current_objfile(), pp)


