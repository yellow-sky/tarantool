env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

MAX_REPLICAS = 3
NUM_INSTANCES = MAX_REPLICAS + 1
BROKEN_QUORUM = NUM_INSTANCES + 1

SERVERS = {}
test_run:cmd("setopt delimiter ';'")
for i=1,MAX_REPLICAS do
    --SERVERS[i] = 'qsync' .. i
test_run:cmd('create server qsync1 with rpl_master=default,\
                                         script="replication/qsync.lua"');
test_run:cmd('start server qsync1 with wait=True, wait_load=True');
end;
test_run:cmd("setopt delimiter ''");
--SERVERS = {'qsync1', 'qsync2', 'qsync3', 'qsync4'}

--test_run:create_cluster(SERVERS, "replication", {args="0.1"})
--test_run:wait_fullmesh(SERVERS)
box.schema.user.grant('guest', 'replication')

-- Successful write.
-- Testcase setup.
test_run:switch('qsync1')
box.info
box.cfg{replication_synchro_quorum=NUM_INSTANCES, replication_synchro_timeout=0.1}
_ = box.schema.space.create('sync', {is_sync=true, engine=engine})
_ = box.space.sync:create_index('pk')
-- Testcase body.
box.space.sync:insert{1} -- success
-- Testcase cleanup.
test_run:switch('default')
box.space.sync:drop()

-- Teardown.
test_run:cmd('switch default')
box.space.sync:drop()
test_run:drop_cluster(SERVERS)
