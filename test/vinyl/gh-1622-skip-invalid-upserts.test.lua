s = box.schema.space.create('test', { engine = 'vinyl', field_count = 2 })
pk = s:create_index('pk')
s:replace{1, 1}
-- Error is logged, upsert is not applied.
--
s:upsert({1, 1}, {{'=', 3, 5}})
-- During read the incorrect upsert is ignored.
--
s:select{}

-- Try to set incorrect field_count in a transaction.
--
box.begin()
s:replace{2, 2}
s:upsert({2, 2}, {{'=', 3, 2}})
s:select{}
box.commit()
s:select{}

-- Read incorrect upsert from a run: it should be ignored.
--
box.snapshot()
s:select{}
s:upsert({2, 2}, {{'=', 3, 20}})
box.snapshot()
s:select{}

-- Execute replace/delete after invalid upsert.
--
box.snapshot()
s:upsert({2, 2}, {{'=', 3, 30}})
s:replace{2, 3}
s:select{}

s:upsert({1, 1}, {{'=', 3, 30}})
s:delete{1}
s:select{}

-- Invalid upsert in a sequence of upserts is skipped meanwhile
-- the rest are applied.
--
box.snapshot()
s:upsert({2, 2}, {{'+', 2, 5}})
s:upsert({2, 2}, {{'=', 3, 40}})
s:upsert({2, 2}, {{'+', 2, 5}})
s:select{}
box.snapshot()
s:select{}

s:drop()