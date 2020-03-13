#!/usr/bin/env tarantool

--[[
Testing plan:

- install tarantool public release version (specified in parameters)
  using packages
- setup a cluster (what topology?) with Tarantool instances
- generate data in cluster
- install new version (specified in parameters) using packages
- restart one instance
- run specific functional checks (TBD) against this instance
- restart the rest instance
- upgrade schema on both instances
- run specific functional checks (TBD) against both(?) instances

]]

--local tarantool = require('tarantool')
local tap = require('tap')
local test = tap.test("upgrade")
local os = require('os')

local function start_server(test)
    test:diag("starting HTTP server on ...")
end

local function stop_server(test, server)
    test:diag("stopping HTTP server")
end

local function test_before_upgrade(test, old_version, new_version)
    test:plan(1)
    test:ok(nil == nil, "body")
end

local function test_after_upgrade(test, old_version, new_version)
    test:plan(1)

    test:ok(nil == nil, "body")
end

test:plan(3)

test:test("upgrade single instance", function(test)
    string.format("Hello, username", "sergeyb")
end)

test:test("upgrade cluster (topology master-master)", function(test)
    string.format("Hello, username", "sergeyb")
end)

test:test("upgrade cluster (topology master-slave)", function(test)
    string.format("Hello, username", "sergeyb")
end)

os.exit(test:check() == true and 0 or -1)
