# Multi-directional iterators

* **Status**: In progress
* **Start date**: 22-01-2020
* **Authors**: Nikita Pettik @korablev77 korablev@tarantool.org
* **Issues**: [#3243](https://github.com/tarantool/tarantool/issues/3243)


## Background and motivation

This RFC touches only Memtx engine and TREE index type (as the only available
in SQL and most common in user's practice). Multi-directional iterator is
an iterator which allows iterating through different key parts orders.
For instance: consider index `i` consisting of three key parts: `a`, `b` and `c`.
Creating and using casual iterator looks like:
```
i = box.space.T.index.i
i:select({1, 2, 3}, {iterator = 'EQ'}) -- returns all tuples which has
                                       -- fields a == 1, b == 2, c == 3.
```
It is OK to omit one or more key parts declaring key value to be searched. In
this case they are assumed to be nils:
`i:select({1}, {iterator = 'EQ'})` is the same as
`i:select({1, nil, nil}, {iterator = 'EQ'})`. So all tuples which has `a == 1`
are getting to the result set. More formally matching rule for nil parts is
following (returns TRUE in case a search key is matched with an index key):
```
if (search-key-part[i] is nil)
{
  if (iterator is LT or GT) return FALSE
  return TRUE
}
```

Another example:
`i:select({1, 1, 1}, {iterator = 'GE'})`

Here all tuples with
`(a = 1 AND b = 1 AND c >= 1) OR (a = 1 AND b > 1) OR (a > 1)`
are returned keeping the lexicographically order.
But some users may want to select tuples with `a >= 1`, `b >= 1` but `c < 1`.
Moreover, somebody may be willing to get tuples ordered by `a` and `b` in
ascending order but by `c` in descending order:
`i:select({}, {iterator = {'GE', 'GE', 'LE'})`, which is analogue of common SQL
query `SELECT * FROM t ORDER BY a ASC, b ASC, c DESC`. Or even query like this:
`SELECT * FROM t WHERE a > 1 AND b < 1 ORDER BY a ASC, b DESC`
which corresponds to Tarantool's
`i:select({1, 1}, {iterator = {'GT', 'LT'})`.
These requests are obviously impossible to fulfill with current indexes and
iterators implementations. This RFC suggests ways to resolve mentioned problem
in particular for Memtx TREE indexes.

## Suggested approach

TREE indexes in memtx engine are implemented as BPS-trees (see
`src/lib/salad/bps_tree.h` for details). Keys correspond to particular values
of key parts; data - to pointers to tuples. Hence, all data are sorted by their
key values due to tree structure. For this reason HASH indexes have only GT and
EQ (and ergo GE) iterators - data stored in a hash is unordered. Tree interface
itself provides several functions to operate on data.  Iteration process starts
in `tree_iterator_start()` (which is called once as `iterator->next()`):
depending on its type iterator is positioned to the lower or upper bound (via
`memtx_tree_lower_bound()`) of range of values satisfying search condition.
In case key is not specified (i.e. empty), iterator is simply set to the first
or the last element of tree. At this moment first element to be returned (if any)
is ready. To continue iterating `next` method of iterator object is changed to one
of `tree_iterator_next()`, `tree_iterator_prev()` or their analogues for GE and
LE iterators. Actually these functions fetch next element from B-tree leaf
block. If iterator points to the last element in the block, it is set to the
first element of the next block (leaf blocks are linked into list); if there's
no more blocks, iterator is invalidated and iteration process is finished.  
Taking into account this information let's consider implementation of
multi-directional iterators.

### Implementation details

First solution doesn't involve any additional data structures so that it deals
with multi-directional iterators only using existing B-tree structure.  
It fact, the first key part can be used to locate the first element as a
candidate in the range to be selected. To illustrate this point let's consider
following Fexample:

```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}}})`
s:insert({1, 0})
s:insert({1, 0})
s:insert({1, 1})
s:insert({2, 0})
s:insert({2, 1})
i:select({}, {iterator = {'GE', 'LE'}})
```

Result should be:
```
[1, 1]
[1, 0]
[1, 0]
[2, 1]
[2, 0]
```
Note that in case of casual GE iterator (i.e. {GE, GE} in terms of
multi-directional iterators) result is:
```
[1, 0]
[1, 0]
[1, 1]
[2, 0]
[2, 1]
```
As one can see, results are sorted in different orders by second key part,
but in the same order by first key part (not surprisingly). Assume first
element with first key part satisfying search condition is located: {1, 0}.
It can be done by creating GE iterator with {nil, nil} search key. Then let's
find out the first key part with different iterating order - in our case it is
second key part. Since the order is different for that key part, it is required
to locate the upper bound of iteration, i.e. {1, 1} tuple. Searching that
element in BPS tree can be processed with creating auxiliary iterator by
`bps_tree_upper_bound({1, nil})`. After auxiliary iterator positioned to that
tuple (see schema below), we can move it towards main one until they match
(calling `bps_tree_iterator_prev` on auxuliary iterator). All keys between these
iterators satisfy search condition.

```
[1, 0],   [1, 0],   [1, 1],   [2, 0] ... // tuples are arranged as in B-tree
^         ^         ^         ^
|         |         |         |
| <- prev() <- prev() Aux.itr |
|                             |
| Main.itr                    |
+---------- next()----------->|
```
Auxiliary iterator is required for each key part starting from that which
direction is different from one of first key part. Let's consider a bit more
sophisticated example with three key parts:
```
s:create_index('i', {parts = {{1, 'integer'}, {2, 'integer'}, {3, integer}}})`
s:insert({0, 0, 0})
s:insert({0, 1, 3})
s:insert({0, 2, 1})
s:insert({0, 2, 3})
s:insert({0, 2, 4})
s:insert({0, 4, 5})
s:insert({1, 1, 1})
i:select({0, 2, 3}, {iterator = {'GE', 'LE', 'GE'}})
```
```
[0, 0, 0],   [0, 1, 3],   [0, 2, 1],   [0, 2, 3],   [0, 2, 4],   [0, 4, 5],   [1, 1, 1]
^                                      ^                         ^
itrA                                  itrC       next()--->     itrB


```
In this case we have to use three iterators: `iterA` over first key part
(created with key {0, nil, nil}), `iterB` over second key part (i.e. with
fixed first key part - upper bound of {0, 2, nil}) and `iterC` over third
key part - lower bound of {0, 2, 3}. At first, `iterA` is positioned at
{0, 0, 0} tuple, `iterB` - at {0, 4, 5}, `iterC` - at {0, 2, 3}. Moving `iterC`
towards `itrB` (invoking `bps_tree_iterator_next`) gives us first part of keys
in range satisfying search condition: [0, 2, 3] and [0, 2, 4]. Then we move
`itrB` to its second position
(i.e. `bps_tree_lower_bound` + `bps_tree_iterator_prev`):
```
[0, 0, 0],   [0, 1, 3],   [0, 2, 1],   [0, 2, 3],   [0, 2, 4],   [0, 4, 5],   [1, 1, 1]
^            ^             
itrA         itrB
             itrC

```
`ItrC` is positioned at key {`itrA_key`, `itrB_key`, 3} == {0, 1, 3}. Since it
matches with `itrB`, tuple is added to result set. After moving `itrB` to the
next position ({0, 0, 0}) it matches with `itrA`, but tuple does not sotisfy
search criteria, so we can move `itrA` to its next position -
`bps_tree_upper_bound({0, nil, nil})` + `bps_tree_iterator_next()`.  
```
...  [0, 4, 5],   [1, 1, 1] ...
                  ^
                 itrA
```
For EQ iterator type it is possible to simply skip those tuples which doesn't
satisfy equality condition. In turn, it results in necessity to extract part of
key value for all 'EQ' iterator key parts and compare it with reference key
value. This algorithm can be generalized for any number of key parts in index.
In the common case we maintain stack of N iterators: iterator which corresponds
to the last key part is placed on the top; on the bottom - to the first key part.
Depending on the order of iteration (LE/GE/LT/GT) we move auxiliary iterators
towards each other. Displacement of iterator corresponding to k-th part of
key def (out of n parts) results in creating `n-k` auxiliary iterators and
pushing them into the stack. When iterator is popped from stack, we move it to
the position of iterator on the top of stack. When these positions match, we
destroy popped iterator and pop the next iterator from the stack.
Using pseudocode:
```
for (int i = 0; i < key_part_count; ++i)
  if (it->type[i] == ITER_GE) {
    //get_iter_key returns proper iteration key for i-th
    memtx_tree_key_data key = get_itr_key(iter, i);
    //depending on iterator's type we call upper/lower bound
    itr_stack.push(bps_tree_upper_bound(key);
  }
}
while (! iter_stack.isEmpty())
{
  iterator itr = itr_stack.pop();
  while (itr.pos != itr_stack.top().pos) {
    // next() or prev() depending on the iterator's type
    itr->next();
  }
}
```
## Rationale and alternatives

Proposed approach allows to specify any sets of key part iteration orders. It
introduces almost no memory overhead in contrast to alternative solution below.
However, it requires re-creating and positioning auxiliary iterators which is
a bit slower than normal iteration over BPS-tree elements.

### Alternative approach

Since BPS tree is built without acknowledge in which order keys should be
placed, it is assumed that order is always ascending: keys in blocks are sorted
from smaller to bigger (left-to-right); comparison between keys is made by
`tuple_compare_with_key()` function. It makes given tree unsuitable for
efficient iteration in different orders. On the other hand, it is possible to
build new *temporary in-memory* data structure (e.g. array) featuring correct
key order. It seems to be easy to achieve since order of keys depends on result
of comparator function. Reverting result of comparison for key parts
corresponding to opposite iteration direction gives appropriate keys ordering
in the tree. Note that not all data in space is needed to be in tree (in case
of non-empty search key); only sub-tree making up lower or upper bound of first
key part is required. The main drawback of this approach is that the first tuple
to be selected is probably returned with significant delay. What is more, we got
tree memory construction overhead (but only during iteration routine).