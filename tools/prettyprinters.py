from __future__ import print_function

import gdb.printing

class VectorPrinter(Iterator):
  """Print a Vector<>"""

  def __init__(self, val):
    self.val = val
    t = val.type.template_argument(0).pointer()
    self.begin = val['ptr'].cast(t)
    self.size = val['sz']
    self.i = 0

  def __next__(self):
    if self.i == self.size:
      raise StopIteration
    ret = '[{}]'.format(self.i), (self.begin+self.i).dereference()
    self.i += 1
    return ret

  def to_string(self):
    return 'Vector of size: {}'.format(self.size)

  def display_hint (self):
    return 'array'


class DenseMatrixPrinter(Iterator):
  """Print a Vector<>"""

  def __init__(self, val):
    self.val = val
    t = val.type.template_argument(0).pointer()
    self.begin = val['ptr'].cast(t)
    self.rows = val['sz']['M']
    self.cols = val['sz']['N']
    self.r = 0
    self.c = -1

  def __next__(self):
    self.c += 1
    if self.c == self.cols:
      self.c = 0
      self.r += 1
      if self.r == self.rows:
        raise StopIteration
    ind = '[{}, {}]'.format(self.r, self.c)
    r = (self.begin+(self.c + self.r*self.cols)).dereference()
    v = str(r)
    if r >= 0:
      v = ' ' + v
    if (self.c == self.cols-1) and (self.r != self.rows-1)
      v += '\n  '
    return ind, v

  def to_string(self):
    return 'Matrix, {} x {}:\n'.format(self.rows, self.cols)
    
  def display_hint (self):
    return 'array'

class StridedMatrixPrinter(Iterator):
  """Print a Vector<>"""

  def __init__(self, val):
    self.val = val
    t = val.type.template_argument(0).pointer()
    self.begin = val['ptr'].cast(t)
    self.rows = val['sz']['M']
    self.cols = val['sz']['N']
    self.stride = val['sz']['strideM']
    self.r = 0
    self.c = -1

  def __next__(self):
    self.c += 1
    if self.c == self.cols:
      self.c = 0
      self.r += 1
      if self.r == self.rows:
        raise StopIteration
    ind = '[{}, {}]'.format(self.r, self.c)
    r = (self.begin+(self.c + self.r*self.stride)).dereference()
    v = str(r)
    if r >= 0:
      v = ' ' + v
    if (self.c == self.cols-1) and (self.r != self.rows-1)
      v += '\n  '
    return ind, v

  def to_string(self):
    return 'Matrix, {} x {}, stride {}:\n'.format(self.rows, self.cols, self.stride)
    
  def display_hint (self):
    return 'array'

pp = gdb.printing.RegexpCollectionPrettyPrinter("LoopModels")
pp.add_printer('LinearAlgebra::Array', '^LinearAlgebra::Array<.*, unsigned int>$', VectorPrinter)
pp.add_printer('LinearAlgebra::Array', '^LinearAlgebra::Array<.*, LinearAlgebra::DenseDims>$', DenseMatrixPrinter)
pp.add_printer('LinearAlgebra::Array', '^LinearAlgebra::Array<.*, LinearAlgebra::StridedDims>$', StridedMatrixPrinter)
gdb.printing.register_pretty_printer(gdb.current_objfile(), pp)


