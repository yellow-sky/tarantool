-- Test upsert execution/squash referring to fields in reversed
-- order (via negative indexing).
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert({1, 1, 1})
box.snapshot()

s:upsert({1}, {{'=', 3, 100}})
s:upsert({1}, {{'=', -1, 200}})
box.snapshot()
s:select() -- {1, 1, 200}

s:delete({1})
s:insert({1, 1, 1})
box.snapshot()

s:upsert({1}, {{'=', -3, 100}})
s:upsert({1}, {{'=', -1, 200}})
box.snapshot()
-- Two upserts are squashed into one which can't be applied since
-- one of its operations modifies PK.
--
s:select() -- {1, 1, 1}

s:delete({1})
box.snapshot()

s:upsert({1, 1}, {{'=', -2, 300}}) -- {1, 1}
s:upsert({1}, {{'+', -1, 100}}) -- {1, 101}
s:upsert({1}, {{'-', 2, 100}}) -- {1, 1}
s:upsert({1}, {{'+', -1, 200}}) -- {1, 201}
s:upsert({1}, {{'-', 2, 200}}) -- {1, 1}
box.snapshot()
s:select() -- {1, 1}

s:delete({1})
box.snapshot()

s:upsert({1, 1, 1}, {{'!', -1, 300}}) -- {1, 1, 1}
s:upsert({1}, {{'+', -2, 100}}) -- {1, 101, 1}
s:upsert({1}, {{'=', -1, 100}}) -- {1, 101, 100}
s:upsert({1}, {{'+', -1, 200}}) -- {1, 101, 300}
s:upsert({1}, {{'-', -2, 100}}) -- {1, 1, 300}
box.snapshot()
s:select()

s:drop()
