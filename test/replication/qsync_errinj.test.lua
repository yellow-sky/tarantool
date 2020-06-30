test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
box.schema.user.grant('guest', 'super')

test_run:cmd('create server replica with rpl_master=default,\
             script="replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

_ = box.schema.space.create('sync', {is_sync = true, engine = engine})
_ = box.space.sync:create_index('pk')

--
-- gh-5123: replica WAL fail shouldn't crash with quorum 1.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 1, replication_synchro_timeout = 5}
box.space.sync:insert{1}

test_run:switch('replica')
box.error.injection.set('ERRINJ_WAL_IO', true)

test_run:switch('default')
box.space.sync:insert{2}

test_run:switch('replica')
test_run:wait_upstream(1, {status='stopped'})
box.error.injection.set('ERRINJ_WAL_IO', false)

test_run:cmd('restart server replica')
box.space.sync:select{2}

test_run:cmd('switch default')

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
}
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

box.space.sync:drop()
box.schema.user.revoke('guest', 'super')
