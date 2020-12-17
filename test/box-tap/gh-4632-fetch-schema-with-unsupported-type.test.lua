#!/usr/bin/env tarantool

local ffi = require('ffi')
local tap = require('tap')
local netbox = require('net.box')

local builtin = ffi.C

ffi.cdef[[
    struct space *space_by_id(uint32_t id);
    void space_run_triggers(struct space *space, bool yesno);
]]

local test = tap.test('gh-4632-fetch-schema-with-unsupported-type')
test:plan(1)

local listen = os.getenv('LISTEN') or 'localhost:3301'
box.cfg{listen = listen}
box.schema.user.grant('guest','read,write,execute,create,drop,alter','universe')
local space_name = 'tweedledum'
local space_id = 1000

-- Setup a space with unknown type in space format.
-- To make it possible we need to disable system triggers.
local s = builtin.space_by_id(space.id)
if s == nil then
    box.error(box.error.NO_SUCH_SPACE, space_name)
end
builtin.space_run_triggers(s, false)
box.space._space:insert{space_id, 1, space_name, 'memtx', 0, setmetatable({}, {__serialize = 'map'}), {{name = 'f1', type = 'string'}}}
box.space[space_name]:create_index('pk')
-- box.space[space_name]:create_index('pk', {parts = {{1, 'decimal'}}})
box.space[space_name]:insert{'tweedledum'}
builtin.space_run_triggers(s, true)

-- Connect to the same Tarantool instance via netbox
-- and make sure instance allow schema fetching with unknown type in space format.
local ok, err = pcall(net_box.connect, listen)
test:ok(ok, 'verify fetching schema with unknown MP_EXT extension', {err = err})
ok, err = pcall(c[space_name]:select{})

-- Teardown
box.space[space_name]:drop()
