#!/usr/bin/env tarantool
local tap = require('tap')
local test = tap.test('execute')
test:plan(4)

--
-- gh-4231: box.execute should be an idempotent function meaning
-- its effect should be the same if the user chooses to save it
-- before explicit box.cfg{} invocation and use the saved version
-- afterwards.
-- Within the scope of the same issue box.cfg method should also
-- be kept idempotent for the same reasons.
--

local box_execute_stub = box.execute
local box_cfg_stub = box.cfg

-- Explicit box configuration that used to change the behavior.
box.cfg{}

local box_execute_actual = box.execute
local box_cfg_actual = box.cfg

local function is_idempotent(method, cmd, msg)
    local status, err = pcall(method, cmd)
    test:ok(status, msg, nil, err)
end

is_idempotent(box_execute_stub,
    "CREATE TABLE t1 (s1 INTEGER, PRIMARY KEY (s1));",
    "box.execute stub works before box.cfg")
is_idempotent(box_execute_actual, "DROP TABLE t1",
    "box.execute works properly after box.cfg")

is_idempotent(box_cfg_stub, nil, "box.cfg stub works properly")
is_idempotent(box_cfg_actual, nil, "box.cfg after configuration")

os.exit(test:check() and 0 or 1)
