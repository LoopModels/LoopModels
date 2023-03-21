from __future__ import print_function

import gdb.printing


class VectorPrinter(Iterator):
    """Print a Vector<>"""

    def __init__(self, val):
        self.val = val
        t = val.type.template_argument(0).pointer()
        self.begin = val["ptr"].cast(t)
        self.size = val["sz"]
        self.i = 0

    def __next__(self):
        if self.i == self.size:
            raise StopIteration
        ret = "[{}]".format(self.i), (self.begin + self.i).dereference()
        self.i += 1
        return ret

    def to_string(self):
        return "Vector of size: {}".format(self.size)

    def display_hint(self):
        return "array"


class BaseMatrixPrinter(Iterator):
    """Print a StridedMatrix<>"""

    def __init__(self, begin, rows, cols, stride):
        self.begin = begin
        self.rows = rows
        self.cols = cols
        self.stride = stride
        self.r = 0
        self.c = -1

    def __next__(self):
        if (self.rows == 0) or (self.cols == 0):
            raise StopIteration
        self.c += 1
        if self.c == self.cols:
            self.c = 0
            self.r += 1
            if self.r == self.rows:
                raise StopIteration
        ind = "[{}, {}]".format(self.r, self.c)
        r = (self.begin + (self.c + self.r * self.stride)).dereference()
        v = str(r)
        if r >= 0:
            v = " " + v
        if (self.c == 0) and (self.r != 0):
            v = "\n    " + v
        if (self.r == self.rows - 1) and (self.c == self.cols - 1):
            v = v + " "
        return ind, v

    def to_string(self):
        return "Matrix, {} x {}, stride {}:\n".format(self.rows, self.cols, self.stride)

    def display_hint(self):
        return "array"


class SquareMatrixPrinter(BaseMatrixPrinter):
    """Print a Matrix<>"""

    def __init__(self, val):
        t = val.type.template_argument(0).pointer()
        BaseMatrixPrinter.__init__(
            self, val["ptr"].cast(t), val["sz"]["M"], val["sz"]["M"], val["sz"]["M"]
        )


class DenseMatrixPrinter(BaseMatrixPrinter):
    """Print a Matrix<>"""

    def __init__(self, val):
        t = val.type.template_argument(0).pointer()
        BaseMatrixPrinter.__init__(
            self, val["ptr"].cast(t), val["sz"]["M"], val["sz"]["N"], val["sz"]["N"]
        )


class StridedMatrixPrinter(BaseMatrixPrinter):
    """Print a Matrix<>"""

    def __init__(self, val):
        t = val.type.template_argument(0).pointer()
        BaseMatrixPrinter.__init__(
            self,
            val["ptr"].cast(t),
            val["sz"]["M"],
            val["sz"]["N"],
            val["sz"]["strideM"],
        )


pp = gdb.printing.RegexpCollectionPrettyPrinter("LoopModels")
pp.add_printer("LinAlg::Array", "^LinAlg::Array<.*, unsigned int>$", VectorPrinter)
pp.add_printer("LinAlg::ManagedArray", "^LinAlg::ManagedArray<.*, unsigned int, .*, std::allocator<.*>, .*>$", VectorPrinter)
pp.add_printer(
    "LinAlg::Array",
    "^LinAlg::Array<.*, LinAlg::SquareDims>$",
    DenseMatrixPrinter,
)
pp.add_printer(
    "LinAlg::Array",
    "^LinAlg::Array<.*, LinAlg::DenseDims>$",
    DenseMatrixPrinter,
)
pp.add_printer(
    "LinAlg::Array",
    "^LinAlg::Array<.*, LinAlg::StridedDims>$",
    StridedMatrixPrinter,
)
gdb.printing.register_pretty_printer(gdb.current_objfile(), pp)
