#!/usr/bin/env tarantool

--
-- gh-4691: net.box fails to connect to tarantool-2.2+ server with
-- a schema of version 2.1.3 or below (w/o _vcollation system
-- space).
--
-- Tarantool does not update a schema automatically when an
-- instance is in a replication cluster, because it may break
-- instances that are run under an old tarantool version. It is
-- quite usual to have non-upgraded instances for some time,
-- because upgrade is usually performed step-by-step.
--
-- net.box leans on a server version from greeting to determine
-- whether _vcollation system view should exist and reports an
-- error if fetching of the space fails. This causes the problem:
-- a version may be 2.2+, but _vcollation does not exists, because
-- schema upgrade is not performed yet. The fix of gh-4691 allows
-- the server to respond ER_NO_SUCH_SPACE for the query.
--

local test_run = require('test_run').new()
local net_box = require('net.box')
local tap = require('tap')

local function before_all()
    local opts = {
        'script = "box-tap/no_auto_schema_upgrade.lua"',
        'workdir = "box-tap/snap/2.1.3"',
        'return_listen_uri = True',
    }
    local opts_str = table.concat(opts, ', ')
    local cmd = 'create server schema_2_1_3 with %s'
    local uri = test_run:cmd(cmd:format(opts_str))
    test_run:cmd('start server schema_2_1_3')

    -- Create 'test' space with 'unicode_ci' index part.
    --
    -- We need it to verify that net.box will expose collation_id
    -- for an index key part when collation names information is
    -- not available.
    --
    -- Note: read_only = false on reconfiguration does not lead to
    -- a schema upgrading.
    test_run:eval('schema_2_1_3', ([[
        box.cfg{read_only = false}
        box.schema.create_space('test')
        box.space.test:create_index('pk', {parts =
            {{field = 1, type = 'string', collation = 'unicode_ci'}}})
        box.cfg{read_only = true}
    ]]):gsub('\n', ' '))
    return uri
end

local function test_connect_schema_2_1_3(test, uri)
    test:plan(3)

    local connection = net_box.connect(uri)

    -- Connection is alive.
    test:ok(connection:is_connected(), 'connection is alive')

    -- Space metainfo is correct: collation_id is exposed when
    -- collation name information is not available.
    local key_part = connection.space.test.index[0].parts[1]
    test:is(key_part.collation, nil,
        'collation names are not available')
    test:is(type(key_part.collation_id), 'number',
        'collation numeric ids are exposed')

    connection:close()
end

local function after_all()
    -- Drop 'test' space.
    test_run:eval('schema_2_1_3', ([[
        box.cfg{read_only = false}
        box.space.test:drop()
        box.cfg{read_only = true}
    ]]):gsub('\n', ' '))

    test_run:cmd('stop server schema_2_1_3')
    test_run:cmd('cleanup server schema_2_1_3')
    test_run:cmd('delete server schema_2_1_3')
end

local uri = before_all()
local test = tap.test('gh-4691-net-box-connect-schema-2-1-3')
test:plan(1)
test:test('connect_schema_2_1_3', test_connect_schema_2_1_3, uri)
after_all()

os.exit(test:check() and 0 or 1)
