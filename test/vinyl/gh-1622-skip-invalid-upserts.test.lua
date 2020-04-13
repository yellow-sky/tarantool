s = box.schema.space.create('test', { engine = 'vinyl', field_count = 2 })
pk = s:create_index('pk')
s:replace{1, 1}
-- Error is logged, upsert is not applied.
--
s:upsert({1, 1}, {{'=', 3, 5}})
-- Invalid upsert still appears during read.
--
s:select{}

s:drop()