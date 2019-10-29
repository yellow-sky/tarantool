#!/usr/bin/env tarantool
local tap = require('tap')
local test = tap.test('execute')
test:plan(2)

--
-- gh-4231: box.execute should be an idempotent function
-- meaning its effect should be the same if a user chooses
-- to use it before explicit box.cfg invocation
--

local function execute_is_immutable(execute, cmd, msg)
    local status, err = pcall(execute, cmd)
    test:ok(status and type(err) == 'table', msg)
end

local box_execute_stub = box.execute
-- explicit call to load_cfg
box.cfg{}
local box_execute_actual = box.execute

execute_is_immutable(box_execute_stub,
    "CREATE TABLE t1 (s1 INTEGER, PRIMARY KEY (s1));",
    "box.execute stub works before box.cfg")
execute_is_immutable(box_execute_actual, "DROP TABLE t1",
    "box.execute works properly after box.cfg")

os.exit(test:check() and 0 or 1)
