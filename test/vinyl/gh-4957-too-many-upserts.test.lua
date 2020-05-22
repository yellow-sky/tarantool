s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert{1, 1}
box.snapshot()

-- Let's test number of upserts in one transaction that exceeds
-- the limit of operations allowed in one update.
--
ups_cnt = 5000
box.begin()
for i = 1, ups_cnt do s:upsert({1}, {{'&', 2, 1}}) end
box.commit()
dump_count = box.stat.vinyl().scheduler.dump_count
tasks_completed = box.stat.vinyl().scheduler.tasks_completed
box.snapshot()

fiber = require('fiber')
while box.stat.vinyl().scheduler.tasks_inprogress > 0 do fiber.sleep(0.01) end

assert(box.stat.vinyl().scheduler.dump_count - dump_count == 1)
-- Last :snapshot() triggers both dump and compaction processes.
--
assert(box.stat.vinyl().scheduler.tasks_completed - tasks_completed == 2)

s:select()

s:drop()

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')

tuple = {}
for i = 1, ups_cnt do tuple[i] = i end
_ = s:insert(tuple)
box.snapshot()

box.begin()
for k = 1, ups_cnt do s:upsert({1}, {{'+', k, 1}}) end
box.commit()
box.snapshot()
while box.stat.vinyl().scheduler.tasks_inprogress > 0 do fiber.sleep(0.01) end

-- All upserts are ignored since they are squashed to one update
-- operation with too many operations.
--
assert(s:select()[1][1] == 1)

s:drop()