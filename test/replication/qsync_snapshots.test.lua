env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
fiber = require('fiber')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout

NUM_INSTANCES = 2
BROKEN_QUORUM = NUM_INSTANCES + 1

box.schema.user.grant('guest', 'replication')

-- Setup an async cluster with two instances.
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

-- [RFC, Snapshot generation] all txns confirmed, then snapshot on master,
-- expected success.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
box.snapshot()
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=orig_synchro_quorum, replication_synchro_timeout=orig_synchro_timeout}
box.space.sync:drop()

-- [RFC, Snapshot generation] all txns confirmed, then snapshot on replica,
-- expected success.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
box.snapshot()
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=orig_synchro_quorum, replication_synchro_timeout=orig_synchro_timeout}
box.space.sync:drop()

-- [RFC, Snapshot generation] rolled back operations are not snapshotted.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('default')
box.cfg{replication_synchro_quorum=3, replication_synchro_timeout=0.1}
box.space.sync:insert{2}
box.snapshot()
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=orig_synchro_quorum, replication_synchro_timeout=orig_synchro_timeout}
box.space.sync:drop()

-- [RFC, Snapshot generation] snapshot started on master, then rollback
-- arrived, expected snapshot abort.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('default')
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=2}
    box.space.sync:insert{2}
end);
test_run:cmd("setopt delimiter ''");
box.snapshot() -- abort
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=orig_synchro_quorum, replication_synchro_timeout=orig_synchro_timeout}
box.space.sync:drop()

-- [RFC, Snapshot generation] snapshot started on replica, then rollback
-- arrived, expected snapshot abort.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
test_run:switch('default')
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=2}
    box.space.sync:insert{2}
end);
test_run:cmd("setopt delimiter ''");
test_run:switch('replica')
box.snapshot() -- abort
box.space.sync:select{} -- 1
test_run:switch('default')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=orig_synchro_quorum, replication_synchro_timeout=orig_synchro_timeout}
box.space.sync:drop()

-- Teardown.
test_run:cmd('switch default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
box.cfg{                                                                        \
    replication_synchro_quorum = orig_synchro_quorum,                           \
    replication_synchro_timeout = orig_synchro_timeout,                         \
}
