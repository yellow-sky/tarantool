#!/usr/bin/env tarantool

--
-- This instance is intended to be used with existing snapshot
-- from a previous tarantool version. It does not perform
-- automatic schema upgrade.
--
-- Having tarantool in this state allows us to create test cases
-- for net.box, relay, applier connected to an instance in such
-- state or test behaviour of the instance itself.
--
-- There are two ways to disable automatic schema upgrade: set
-- 'replication' box.cfg(<...>) option or set 'read_only' option.
-- Use the latter one, because it is simpler.
--

box.cfg({
    listen = os.getenv('LISTEN'),
    read_only = true,
})

-- Give 'guest' user read/write accesses to all spaces.
--
-- Note: reconfiguration with read_only = false does not lead to
-- schema upgrading.
box.cfg{read_only = false}
box.schema.user.grant('guest', 'read, write', 'universe')
box.cfg{read_only = true}

require('console').listen(os.getenv('ADMIN'))
