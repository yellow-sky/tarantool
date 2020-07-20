s = box.schema.space.create('test', { engine = 'vinyl', field_count = 2 })
pk = s:create_index('pk')
s:replace{1, 1}
-- Error is logged, upsert is not applied.
--
s:upsert({1, 1}, {{'=', 3, 5}})
-- Invalid upsert still appears during read.
--
s:select{}

format = {{'f1', 'unsigned'}, {'f2', 'unsigned'}}
s1 = box.schema.space.create('test1', { engine = 'vinyl', format = format })
_ = s:create_index('pk')
s:replace{1, 1}
box.snapshot()

s:upsert({2, 0}, {{'+', 2, 1}})
s:upsert({2, 0}, {{'-', 2, 2}})
box.snapshot()
s:select()

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert({1, 2})
box.snapshot()
s:upsert({1, 0}, {{'+', 2, 1}})
s:upsert({1, 0}, {{'-', 2, 2}})

box.snapshot()
s:select() -- [1, 1]


s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert({1, 2})
box.snapshot()
s:upsert({1, 0}, {{'-', 2, 1}})
s:upsert({1, 0}, {{'-', 2, 2}})

box.snapshot()
s:select() -- [1, 1]

s:drop()