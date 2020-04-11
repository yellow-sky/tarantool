--- tap.lua internal file
---
--- The Test Anything Protocol vesion 13 producer
---

-- yaml formatter must be able to encode any Lua variable
local yaml = require('yaml').new()
yaml.cfg{
    encode_invalid_numbers = true;
    encode_load_metatables = true;
    encode_use_tostring    = true;
    encode_invalid_as_nil  = true;
}

local ffi = require('ffi') -- for iscdata

local function traceback(level)
    local trace = {}
    level = level or 3
    while true do
        local info = debug.getinfo(level, "nSl")
        if not info then break end
        local frame = {
            source = info.source;
            src = info.short_src;
            line = info.linedefined or 0;
            what = info.what;
            name = info.name;
            namewhat = info.namewhat;
            filename = info.source:sub(1, 1) == "@" and info.source:sub(2) or
                'eval'
        }
        table.insert(trace, frame)
        level = level + 1
    end
    return trace
end

local function diag(test, fmt, ...)
    io.write(string.rep(' ', 4 * test.level), "# ", string.format(fmt, ...),
        "\n")
end

local function ok(test, cond, message, extra)
    test.total = test.total + 1
    io.write(string.rep(' ', 4 * test.level))
    if cond then
        io.write(string.format("ok - %s\n", message))
        return true
    end

    test.failed = test.failed + 1
    io.write(string.format("not ok - %s\n", message))
    extra = extra or {}
    if test.trace then
        extra.trace = traceback()
        extra.filename = extra.trace[#extra.trace].filename
        extra.line = extra.trace[#extra.trace].line
    end
    if next(extra) == nil then
        return false -- don't have extra information
    end
    -- print aligned yaml output
    for line in yaml.encode(extra):gmatch("[^\n]+") do
        io.write(string.rep(' ', 2 + 4 * test.level), line, "\n")
    end
    return false
end

local function fail(test, message, extra)
    return ok(test, false, message, extra)
end

local function skip(test, message, extra)
    ok(test, true, message.." # skip", extra)
end

local function cmpdeeply(got, expected, extra)
    if type(expected) == "number" or type(got) == "number" then
        extra.got = got
        extra.expected = expected
        if got ~= got and expected ~= expected then
            return true -- nan
        end
        return got == expected
    end

    if ffi.istype('bool', got) then got = (got == 1) end
    if ffi.istype('bool', expected) then expected = (expected == 1) end

    if extra.strict and type(got) ~= type(expected) then
        extra.got = type(got)
        extra.expected = type(expected)
        return false
    end

    if type(got) ~= 'table' or type(expected) ~= 'table' then
        extra.got = got
        extra.expected = expected
        return got == expected
    end

    local path = extra.path or '/'
    local visited_keys = {}

    for i, v in pairs(got) do
        visited_keys[i] = true
        extra.path = path .. '/' .. i
        if not cmpdeeply(v, expected[i], extra) then
            return false
        end
    end

    -- check if expected contains more keys then got
    for i, v in pairs(expected) do
        if visited_keys[i] ~= true and (extra.strict or v ~= box.NULL) then
            extra.expected = 'key ' .. tostring(i)
            extra.got = 'nil'
            return false
        end
    end

    extra.path = path

    return true
end

local function like(test, got, pattern, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = pattern
    return ok(test, string.match(tostring(got), pattern) ~= nil, message, extra)
end

local function unlike(test, got, pattern, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = pattern
    return ok(test, string.match(tostring(got), pattern) == nil, message, extra)
end

local function is(test, got, expected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = expected
    local rc = (test.strict == false or type(got) == type(expected)) and
               got == expected
    return ok(test, rc, message, extra)
end

local function isnt(test, got, unexpected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.unexpected = unexpected
    local rc = (test.strict == true and type(got) ~= type(unexpected)) or
               got ~= unexpected
    return ok(test, rc, message, extra)
end


local function is_deeply(test, got, expected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = expected
    extra.strict = test.strict
    return ok(test, cmpdeeply(got, expected, extra), message, extra)
end

local function isnil(test, v, message, extra)
    return is(test, not v and 'nil' or v, 'nil', message, extra)
end

local function isnumber(test, v, message, extra)
    return is(test, type(v), 'number', message, extra)
end

local function isstring(test, v, message, extra)
    return is(test, type(v), 'string', message, extra)
end

local function istable(test, v, message, extra)
    return is(test, type(v), 'table', message, extra)
end

local function isboolean(test, v, message, extra)
    return is(test, type(v), 'boolean', message, extra)
end

local function isfunction(testcase, v, message, extra)
    return is(testcase, type(v), 'function', message, extra)
end

local function isudata(testcase, v, utype, message, extra)
    extra = extra or {}
    extra.expected = 'userdata<'..utype..'>'
    if type(v) == 'userdata' then
        extra.got = 'userdata<'..getmetatable(v)..'>'
        return ok(testcase, getmetatable(v) == utype, message, extra)
    else
        extra.got = type(v)
        return fail(testcase, message, extra)
    end
end

local function iscdata(testcase, v, ctype, message, extra)
    extra = extra or {}
    extra.expected = ffi.typeof(ctype)
    if type(v) == 'cdata' then
        extra.got = ffi.typeof(v)
        return ok(testcase, ffi.istype(ctype, v), message, extra)
    else
        extra.got = type(v)
        return fail(testcase, message, extra)
    end
end

local test_mt
local function test(parent, name, fun, ...)
    local level = parent ~= nil and parent.level + 1 or 0
    local testcase = setmetatable({
        parent  = parent;
        name    = name;
        level   = level;
        total   = 0;
        failed  = 0;
        planned = 0;
        trace   = parent == nil and true or parent.trace;
        strict = false;
    }, test_mt)
    if fun ~= nil then
        testcase:diag('%s', testcase.name)
        fun(testcase, ...)
        testcase:diag('%s: end', testcase.name)
        return testcase:check()
    else
        return testcase
    end
end

local function plan(testcase, planned)
    testcase.planned = planned
    io.write(string.rep(' ', 4 * testcase.level), string.format("1..%d\n", planned))
end

local function check(testcase)
    if testcase.checked then
        error('check called twice')
    end
    testcase.checked = true
    if testcase.planned ~= testcase.total then
        if testcase.parent ~= nil then
            ok(testcase.parent, false, "bad plan", { planned = testcase.planned;
                run = testcase.total})
        else
            diag(testcase, string.format("bad plan: planned %d run %d",
                testcase.planned, testcase.total))
        end
    elseif testcase.failed > 0 then
        if testcase.parent ~= nil then
            ok(testcase.parent, false, "failed subtests", {
                failed = testcase.failed;
                planned = testcase.planned;
            })
        else
            diag(testcase, "failed subtest: %d", testcase.failed)
        end
    else
        if testcase.parent ~= nil then
            ok(testcase.parent, true, testcase.name)
        end
    end
    return testcase.planned == testcase.total and testcase.failed == 0
end

test_mt = {
    __index = {
        test      = test;
        plan      = plan;
        check     = check;
        diag      = diag;
        ok        = ok;
        fail      = fail;
        skip      = skip;
        is        = is;
        isnt      = isnt;
        isnil     = isnil;
        isnumber  = isnumber;
        isstring  = isstring;
        istable   = istable;
        isboolean = isboolean;
        isfunction = isfunction;
        isudata   = isudata;
        iscdata   = iscdata;
        is_deeply = is_deeply;
        like      = like;
        unlike    = unlike;
    }
}

local function root_test(...)
    io.write('TAP version 13', '\n')
    return test(nil, ...)
end

return {
    test = root_test;
}
