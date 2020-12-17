#!/usr/bin/env tarantool

local tap = require('tap')
local netbox = require('net.box')

local test = tap.test('gh-4632-fetch-schema-with-unsupported-type')
test:plan(1)

local listen = os.getenv('LISTEN') or 'localhost:3301'
box.cfg{listen = listen}
-- box.schema.user.grant('guest','read,write,execute,create,drop,alter','universe')                                                        
local space_name = 'tweedledum'
box.space._space:insert{1000, 1, space_name, 'memtx', 0, setmetatable({}, {__serialize = 'map'}), {{name = 'f1', type = 'string'}}}
box.space[space_name]:create_index('pk')
box.space[space_name]:insert{'tweedledum'}
-- box.space.s:create_index('pk', {parts = {{1, 'decimal'}}})

-- c = net_box.connect(listen)
-- c[space_name]:select{}

local ok, res = pcall(net_box.connect, listen)
test:ok(ok, 'verify load_cfg after box.cfg() call', {err = res})
test:is(box.cfg.read_only, true, 'verify that load_cfg reconfigures box')

box.space[space_name]:drop()

