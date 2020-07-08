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

-- Setup an cluster with two instances.
test_run:cmd('create server replica with rpl_master=default,\
                                         script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

-- [RFC, quorum commit] Attempt to write multiple transactions, expected the
-- same order as on client in case of achieved quorum.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1}
box.space.sync:insert{2}
box.space.sync:insert{3}
box.space.sync:select{} -- 1, 2, 3
test_run:switch('replica')
box.space.sync:select{} -- 1, 2, 3
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Synchro timeout is not less than replication_synchro_timeout value.
-- Testcase setup.
test_run:switch('default')
box.cfg{replication_synchro_quorum=BROKEN_QUORUM, replication_synchro_timeout=orig_synchro_timeout}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
start = os.time()
box.space.sync:insert{1}
(os.time() - start) >= box.cfg.replication_synchro_timeout -- true
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- [RFC, summary] switch sync into async mode in space, expected success and
-- data consistency on a leader and replicas.
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
-- Disable synchronous mode.
disable_sync_mode()
-- Space is in async mode now.
box.cfg{replication_synchro_quorum=NUM_INSTANCES}
box.space.sync:insert{2} -- success
box.space.sync:insert{3} -- success
box.cfg{replication_synchro_quorum=BROKEN_QUORUM}
box.space.sync:insert{4} -- success
box.cfg{replication_synchro_quorum=NUM_INSTANCES}
box.space.sync:insert{5} -- success
box.space.sync:select{} -- 1, 2, 3, 4, 5
test_run:cmd('switch replica')
box.space.sync:select{} -- 1, 2, 3, 4, 5
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- should be greater than number of instances in a cluster, see gh-5122.
box.cfg{replication_synchro_quorum=BROKEN_QUORUM}
-- expected warning, to be add in gh-5122
--while test_run:grep_log('default', 'warning: .*') == nil do fiber.sleep(0.01) end

-- [RFC, summary] switch from leader to replica and vice versa, expected
-- success and data consistency on a leader and replicas (gh-5124).
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
box.cfg{read_only=false} -- promote replica to master
test_run:switch('default')
box.cfg{read_only=true} -- demote master to replica
test_run:switch('replica')
box.space.sync:insert{2}
box.space.sync:select{} -- 1, 2
test_run:switch('default')
box.space.sync:select{} -- 1, 2
-- Revert cluster configuration.
test_run:switch('default')
box.cfg{read_only=false}
test_run:switch('replica')
box.cfg{read_only=true}
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
