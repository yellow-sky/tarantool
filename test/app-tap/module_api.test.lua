#!/usr/bin/env tarantool

local fio = require('fio')

box.cfg{log = "tarantool.log"}
build_path = os.getenv("BUILDDIR")
package.cpath = fio.pathjoin(build_path, 'test/app-tap/?.so'   ) .. ';' ..
                fio.pathjoin(build_path, 'test/app-tap/?.dylib') .. ';' ..
                package.cpath

local function test_pushcdata(test, module)
    test:plan(6)
    local ffi = require('ffi')
    ffi.cdef('struct module_api_test { int a; };')
    local gc_counter = 0;
    local ct = ffi.typeof('struct module_api_test')
    ffi.metatype(ct, {
        __tostring = function(obj)
            return 'ok'
        end;
        __gc = function(obj)
            gc_counter = gc_counter + 1;
        end
    })

    local ctid = tonumber(ct)
    local obj, ptr = module.pushcdata(ctid)
    test:is(ffi.typeof(obj), ct, 'pushcdata typeof')
    test:is(tostring(obj), 'ok', 'pushcdata metatable')
    local ctid2, ptr2 = module.checkcdata(obj)
    test:is(ctid, ctid2, 'checkcdata type')
    test:is(ptr, ptr2, 'checkcdata value')
    test:is(gc_counter, 0, 'pushcdata gc')
    obj = nil
    collectgarbage('collect')
    test:is(gc_counter, 1, 'pushcdata gc')
end

local function test_buffers(test, module)
    test:plan(7)
    local ffi = require('ffi')
    local buffer = require('buffer')

    local ibuf = buffer.ibuf()
    local pbuf = ibuf:alloc(128)

    test:ok(not module.checkibuf(nil), 'checkibuf of nil')
    test:ok(not module.checkibuf({}), 'checkibuf of {}')
    test:ok(not module.checkibuf(1LL), 'checkibuf of 1LL')
    test:ok(not module.checkibuf(box.NULL), 'checkibuf of box.NULL')
    test:ok(not module.checkibuf(buffer.reg1), 'checkibuf of reg1')
    test:ok(module.checkibuf(ibuf), 'checkibuf of ibuf')
    test:ok(not module.checkibuf(pbuf), 'checkibuf of pointer to ibuf data')
end

local function test_tuples(test, module)
    test:plan(8)

    local nottuple1 = {}
    local nottuple2 = {1, 2}
    local nottuple3 = {1, nil, 2}
    local nottuple4 = {1, box.NULL, 2, 3}
    local tuple1 = box.tuple.new(nottuple1)
    local tuple2 = box.tuple.new(nottuple2)
    local tuple3 = box.tuple.new(nottuple3)
    local tuple4 = box.tuple.new(nottuple4)

    test:ok(not module.tuple_validate(nottuple1), "not tuple 1")
    test:ok(not module.tuple_validate(nottuple2), "not tuple 2")
    test:ok(not module.tuple_validate(nottuple3), "not tuple 3")
    test:ok(not module.tuple_validate(nottuple4), "not tuple 4")
    test:ok(module.tuple_validate(tuple1), "tuple 1")
    test:ok(module.tuple_validate(tuple2), "tuple 2")
    test:ok(module.tuple_validate(tuple3), "tuple 3")
    test:ok(module.tuple_validate(tuple4), "tuple 4")
end

local test = require('tap').test("module_api", function(test)
    test:plan(26)
    local status, module = pcall(require, 'module_api')
    test:is(status, true, "module")
    test:ok(status, "module is loaded")
    if not status then
        test:diag("Failed to load library:")
        for _, line in ipairs(module:split("\n")) do
            test:diag("%s", line)
        end
        return
    end

    local space  = box.schema.space.create("test")
    space:create_index('primary')

    for name, fun in pairs(module) do
        if string.sub(name,1, 5) == 'test_' then
            test:ok(fun(), name .. " is ok")
        end
    end

    local status, msg = pcall(module.check_error)
    test:like(msg, 'luaT_error', 'luaT_error')

    test:test("pushcdata", test_pushcdata, module)
    test:test("buffers", test_buffers, module)
    test:test("validate", test_tuples, module)

    space:drop()
end)

os.exit(0)
