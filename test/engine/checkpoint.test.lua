env = require('test_run')
test_run = env.new()

-- test ddl while snapshoting
test_run:cmd('create server ddl with script = "engine/ddl.lua"')
test_run:cmd("start server ddl")
test_run:cmd("switch ddl")
fiber = require('fiber')
engine = test_run:get_cfg('engine')
_ = box.schema.space.create('ddl', {engine = engine}):create_index('pk')
for i = 1, 20 do box.space.ddl:replace({i}) end
errinj = box.error.injection
errinj.set('ERRINJ_SNAP_WRITE_ROW_TIMEOUT', 0.01)
ch = fiber.channel(1)
_ = fiber.create(function () box.snapshot() ch:put(true) end)
-- snapshot was started, remember the current xlog
fio = require('fio')
xlog1 = fio.pathjoin(box.cfg.wal_dir, string.format("%020d.xlog", box.info.lsn))
vylog1 = fio.pathjoin(box.cfg.wal_dir, string.format("%020d.vylog", box.info.lsn))
box.space.ddl:truncate()
errinj.set('ERRINJ_SNAP_WRITE_ROW_TIMEOUT', 0.00)
ch:get()
box.space.ddl:select()
xlog2 = fio.pathjoin(box.cfg.wal_dir, string.format("%020d.xlog", box.info.lsn))
vylog2 = fio.pathjoin(box.cfg.wal_dir, string.format("%020d.vylog", box.info.lsn))
test_run:cmd("switch default")
xlog1 = test_run:eval('ddl', 'return xlog1')[1]
xlog2 = test_run:eval('ddl', 'return xlog2')[1]
vylog1 = test_run:eval('ddl', 'return vylog1')[1]
vylog2 = test_run:eval('ddl', 'return vylog2')[1]
test_run:cmd("stop server ddl")
-- delete last wals in order to check truncated data remains in a snapshot
fio = require('fio')
fio.unlink(xlog1)
fio.unlink(xlog2)
_ = fio.unlink(vylog1)
_ = fio.unlink(vylog2)
-- start server ddl from snapshot only
test_run:cmd('start server ddl')
test_run:cmd("switch ddl")
box.space.ddl:select()
test_run:cmd("switch default")
test_run:cmd("stop server ddl")
test_run:cmd('cleanup server ddl')
