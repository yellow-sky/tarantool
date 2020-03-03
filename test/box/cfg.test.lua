env = require('test_run')
test_run = env.new()
test_run:cmd("push filter '(error: .*)\\.lua:[0-9]+: ' to '\\1.lua:<line>: '")
box.cfg.nosuchoption = 1
cfg_filter(box.cfg)
-- must be read-only
box.cfg()
cfg_filter(box.cfg)

-- check that cfg with unexpected parameter fails.
box.cfg{sherlock = 'holmes'}

-- check that cfg with unexpected type of parameter fails
box.cfg{listen = {}}
box.cfg{wal_dir = 0}
box.cfg{coredump = 'true'}

-- check comment to issue #2191 - bad argument #2 to ''uri_parse''
box.cfg{replication = {}}
box.cfg{replication = {}}

--------------------------------------------------------------------------------
-- Test of hierarchical cfg type check
--------------------------------------------------------------------------------

box.cfg{memtx_memory = "100500"}
box.cfg{memtx_memory = -1}

--------------------------------------------------------------------------------
-- Dynamic configuration check
--------------------------------------------------------------------------------

replication_sync_lag = box.cfg.replication_sync_lag
box.cfg{replication_sync_lag = 0.123}
box.cfg.replication_sync_lag
box.cfg{replication_sync_lag = replication_sync_lag}

replication_sync_timeout = box.cfg.replication_sync_timeout
box.cfg{replication_sync_timeout = 123}
box.cfg.replication_sync_timeout
box.cfg{replication_sync_timeout = replication_sync_timeout}

box.cfg{instance_uuid = box.info.uuid}
box.cfg{instance_uuid = '12345678-0123-5678-1234-abcdefabcdef'}

box.cfg{replicaset_uuid = box.info.cluster.uuid}
box.cfg{replicaset_uuid = '12345678-0123-5678-1234-abcdefabcdef'}

box.cfg{memtx_memory = box.cfg.memtx_memory}
box.cfg{sql_cache_size = box.cfg.sql_cache_size}

--------------------------------------------------------------------------------
-- Test of default cfg options
--------------------------------------------------------------------------------

test_run:cmd('create server cfg_tester1 with script = "box/lua/cfg_test1.lua"')
test_run:cmd("start server cfg_tester1")
test_run:cmd('switch cfg_tester1')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester1")
test_run:cmd("cleanup server cfg_tester1")

test_run:cmd('create server cfg_tester2 with script = "box/lua/cfg_test2.lua"')
test_run:cmd("start server cfg_tester2")
test_run:cmd('switch cfg_tester2')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester2")
test_run:cmd("cleanup server cfg_tester2")

--------------------------------------------------------------------------------
-- Check fix for pid_file option overwritten by tarantoolctl
--------------------------------------------------------------------------------

test_run:cmd('create server cfg_tester5 with script = "box/lua/cfg_test1.lua"')
test_run:cmd("start server cfg_tester5")
test_run:cmd('switch cfg_tester5')
box.cfg{pid_file = "current.pid"}
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester5")
test_run:cmd("cleanup server cfg_tester5")

--
-- gh-3320: box.cfg{net_msg_max}.
--
box.cfg{net_msg_max = 'invalid'}
--
-- gh-3425: incorrect error message: must not contain 'iproto'.
--
box.cfg{net_msg_max = 0}
old = box.cfg.net_msg_max
box.cfg{net_msg_max = 2}
box.cfg{net_msg_max = old + 1000}
box.cfg{net_msg_max = old}

test_run:cmd("clear filter")
