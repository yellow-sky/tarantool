env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
fiber = require('fiber')

orig_synchro_quorum = box.cfg.replication_synchro_quorum
orig_synchro_timeout = box.cfg.replication_synchro_timeout
orig_replication_timeout = box.cfg.replication_timeout

NUM_INSTANCES = 2
BROKEN_QUORUM = NUM_INSTANCES + 1

test_run:cmd("setopt delimiter ';'")
disable_sync_mode = function()
    local s = box.space._space:get(box.space.sync.id)
    local new_s = s:update({{'=', 6, {is_sync=false}}})
    box.space._space:replace(new_s)
end;
test_run:cmd("setopt delimiter ''");

box.schema.user.grant('guest', 'replication')

-- Setup an async cluster with two instances.
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

-- Updated replication_synchro_quorum doesn't affect existed tx.
-- Testcase setup.
test_run:switch('default')
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=0.0001}
box.error.injection.set('ERRINJ_SYNC_TIMEOUT', true)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.space.sync:insert{1}
end);
test_run:cmd("setopt delimiter ''");
box.cfg{replication_synchro_quorum=NUM_INSTANCES}
box.error.injection.set('ERRINJ_SYNC_TIMEOUT', false)
box.space.sync:select{} -- none
test_run:switch('replica')
box.space.sync:select{} -- none
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, Synchronous replication enabling] "As soon as last operation of
-- synchronous transaction appeared in leader's WAL, it will cause all
-- following transactions - no matter if they are synchronous or not - wait for
-- the quorum. In case quorum is not achieved the 'rollback' operation will
-- cause rollback of all transactions after the synchronous one."
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
test_run:switch('default')
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=0.1}
box.error.injection.set('ERRINJ_SYNC_TIMEOUT', true)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.space.sync:insert{2}
end);
test_run:cmd("setopt delimiter ''");
-- Disable synchronous mode.
disable_sync_mode()
-- Space is in async mode now.
box.space.sync:insert{3} -- async operation must wait sync one
box.error.injection.set('ERRINJ_SYNC_TIMEOUT', false)
box.space.sync:select{} -- 1
test_run:cmd('switch replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Check behaviour with failed write to WAL on master (ERRINJ_WAL_IO).
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
box.error.injection.set('ERRINJ_WAL_IO', true)
box.space.sync:insert{2}
box.error.injection.set('ERRINJ_WAL_IO', false)
box.space.sync:select{} -- 1
test_run:switch('replica')
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, quorum commit] check behaviour with failure answer from a replica
-- (ERRINJ_WAL_SYNC) during write, expected disconnect from the replication
-- (gh-5123, set replication_synchro_quorum to 1).
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=2, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:select{} -- 1
test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', true)
test_run:switch('default')
box.space.sync:insert{2}
test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', false)
box.space.sync:select{} -- 1
-- Testcase cleanup.
test_run:switch('default')
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
