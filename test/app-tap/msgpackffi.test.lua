#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local tap = require('tap')
local common = require('serializer_test')
local ffi = require('ffi')
local msgpack = require('msgpack')

local function is_map(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x80 and b <= 0x8f or b == 0xde or b == 0xdf
end

local function is_array(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x90 and b <= 0x9f or b == 0xdc or b == 0xdd
end

local function test_offsets(test, s)
    test:plan(6)
    local arr1 = {1, 2, 3}
    local arr2 = {4, 5, 6}
    local dump = s.encode(arr1)..s.encode(arr2)
    test:is(dump:len(), 8, "length of part1 + part2")

    local a
    local offset = 1
    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr1, "decoded part1")
    test:is(offset, 5, "offset of part2")

    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr2, "decoded part2")
    test:is(offset, 9, "offset of end")

    test:ok(not pcall(s.decode, dump, offset), "invalid offset")
end

local function test_configs(test, s)
    test:plan(26)
    --
    -- gh-4499: ffi module should consider msgpack.cfg options
    --

    -- only excessively sparse arrays should be encoded as maps,
    -- i.e. encode_sparse_ratio > 0 && max_idx > encode_sparse_safe
    -- && max_idx > num(elements) * encode_sparse_ratio
    test:ok(is_array(s.encode({ [1] = 1, [3] = 3, [4] = 3 })),
        "not excessively sparse")
    test:ok(is_array(s.encode({ [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 5,
        [11] = 6 })), "still not excessively sparse")
    test:ok(is_map(s.encode({ [1] = 1, [2] = 2, [100] = 3 })),
        "is excessively sparse")

    msgpack.cfg({encode_sparse_convert = false})
    local ok, _ = pcall(s.encode(), { [1] = 1, [2] = 2, [100] = 3 })
    test:ok(not ok, "conversion of sparse tables is manually prohibited")

    -- testing (en/de)coding of invalid numbers
    local inf = 1/0
    local minf = -1/0
    local nan = 0/0

    test:ok(s.decode(s.encode(inf)), "decode & encode for inf")
    test:ok(s.decode(s.encode(minf)), "decode & encode for minf")
    test:ok(s.decode(s.encode(nan)), "decode & encode for nan")

    msgpack.cfg({encode_invalid_numbers = false})
    test:ok(not pcall(s.encode, inf), "prohibited encode for inf")
    test:ok(not pcall(s.encode, minf), "prohibited encode for minf")
    test:ok(not pcall(s.encode, nan), "prohibited encode for nan")

    msgpack.cfg({decode_invalid_numbers = false})
    test:ok(not pcall(s.decode, inf), "prohibited decode for inf")
    test:ok(not pcall(s.decode, minf), "prohibited decode for minf")
    test:ok(not pcall(s.decode, nan), "prohibited decode for nan")

    -- invalid numbers can also be encoded as nil values
    msgpack.cfg({decode_invalid_numbers = true, encode_invalid_as_nil = true})
    test:is(s.decode(s.encode(inf)), nil, "invalid_as_nil for inf")
    test:is(s.decode(s.encode(minf)), nil, "invalid_as_nil for minf")
    test:is(s.decode(s.encode(nan)), nil, "invalid_as_nil for nan")

    -- whether __serialize meta-value checking is enabled
    local arr = setmetatable({1, 2, 3, k1 = 'v1', k2 = 'v2', 4, 5},
        { __serialize = 'seq'})
    local map = setmetatable({1, 2, 3, 4, 5}, { __serialize = 'map'})
    local obj = setmetatable({}, {
        __serialize = function(x) return 'serialize' end
    })

    -- testing encode metatables
    msgpack.cfg{encode_load_metatables = false}
    test:ok(is_map(s.encode(arr)), "array ignore __serialize")
    test:ok(is_array(s.encode(map)), "map ignore __serialize")
    test:ok(is_array(s.encode(obj)), "object ignore __serialize")

    msgpack.cfg{encode_load_metatables = true}
    test:ok(is_array(s.encode(arr)), "array load __serialize")
    test:ok(is_map(s.encode(map)), "map load __serialize")
    test:is(s.decode(s.encode(obj)), "serialize", "object load __serialize")

    -- testing decode metatables
    msgpack.cfg{decode_save_metatables = false}
    test:isnil(getmetatable(s.decode(s.encode(arr))), "array __serialize")
    test:isnil(getmetatable(s.decode(s.encode(map))), "map __serialize")

    msgpack.cfg{decode_save_metatables = true}
    test:is(getmetatable(s.decode(s.encode(arr))).__serialize, "seq",
        "array save __serialize")
    test:is(getmetatable(s.decode(s.encode(map))).__serialize, "map",
        "map save __serialize")

    -- applying default configs
    msgpack.cfg({encode_sparse_convert = true, encode_invalid_numbers = true,
    encode_invalid_as_nil = false})
end

local function test_other(test, s)
    test:plan(24)
    local buf = string.char(0x93, 0x6e, 0xcb, 0x42, 0x2b, 0xed, 0x30, 0x47,
        0x6f, 0xff, 0xff, 0xac, 0x77, 0x6b, 0x61, 0x71, 0x66, 0x7a, 0x73,
        0x7a, 0x75, 0x71, 0x71, 0x78)
    local num = s.decode(buf)[2]
    test:ok(num < 59971740600 and num > 59971740599, "gh-633 double decode")

    -- gh-596: msgpack and msgpackffi have different behaviour
    local arr = {1, 2, 3}
    local map = {k1 = 'v1', k2 = 'v2', k3 = 'v3'}
    test:is(getmetatable(s.decode(s.encode(arr))).__serialize, "seq",
        "array save __serialize")
    test:is(getmetatable(s.decode(s.encode(map))).__serialize, "map",
        "map save __serialize")

    -- gh-1095: `-128` is packed as `d1ff80` instead of `d080`
    test:is(#s.encode(0x7f), 1, "len(encode(0x7f))")
    test:is(#s.encode(0x80), 2, "len(encode(0x80))")
    test:is(#s.encode(0xff), 2, "len(encode(0xff))")
    test:is(#s.encode(0x100), 3, "len(encode(0x100))")
    test:is(#s.encode(0xffff), 3, "len(encode(0xffff))")
    test:is(#s.encode(0x10000), 5, "len(encode(0x10000))")
    test:is(#s.encode(0xffffffff), 5, "len(encode(0xffffffff))")
    test:is(#s.encode(0x100000000), 9, "len(encode(0x100000000))")
    test:is(#s.encode(-0x20), 1, "len(encode(-0x20))")
    test:is(#s.encode(-0x21), 2, "len(encode(-0x21))")
    test:is(#s.encode(-0x80), 2, "len(encode(-0x80))")
    test:is(#s.encode(-0x81), 3, "len(encode(-0x81))")
    test:is(#s.encode(-0x8000), 3, "len(encode(-0x8000))")
    test:is(#s.encode(-0x8001), 5, "len(encode(-0x8001))")
    test:is(#s.encode(-0x80000000), 5, "len(encode(-0x80000000))")
    test:is(#s.encode(-0x80000001), 9, "len(encode(-0x80000001))")

    --
    -- gh-4434: msgpackffi does not care about msgpack serializer
    -- configuration, but it should.
    --
    local function check_depth(depth_to_try)
        local t = nil
        for i = 1, depth_to_try do t = {t} end
        t = s.decode_unchecked(s.encode(t))
        local level = 0
        while t ~= nil do level = level + 1 t = t[1] end
        return level
    end
    local deep_as_nil = msgpack.cfg.encode_deep_as_nil
    msgpack.cfg({encode_deep_as_nil = true})
    local max_depth = msgpack.cfg.encode_max_depth
    local result_depth = check_depth(max_depth + 5)
    test:is(result_depth, max_depth,
            "msgpackffi uses msgpack.cfg.encode_max_depth")

    msgpack.cfg({encode_max_depth = max_depth + 5})
    result_depth = check_depth(max_depth + 5)
    test:is(result_depth, max_depth + 5, "and uses it dynamically")

    -- Recursive tables are handled correctly.
    local level = 0
    local t = {}
    t[1] = t
    t = s.decode(s.encode(t))
    while t ~= nil do level = level + 1 t = t[1] end
    test:is(level, max_depth + 5, "recursive array")
    t = {}
    t.key = t
    t = s.decode(s.encode(t))
    level = 0
    while t ~= nil do level = level + 1 t = t.key end
    test:is(level, max_depth + 5, "recursive map")

    msgpack.cfg({encode_deep_as_nil = false})
    local ok = pcall(check_depth, max_depth + 6)
    test:ok(not ok, "exception is thrown when crop is not allowed")

    msgpack.cfg({encode_deep_as_nil = deep_as_nil,
                 encode_max_depth = max_depth})
end

tap.test("msgpackffi", function(test)
    local serializer = require('msgpackffi')
    test:plan(12)
    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("decimal", common.test_decimal, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    -- udata/cdata hooks are not implemented
    --test:test("ucdata", common.test_ucdata, serializer)
    test:test("offsets", test_offsets, serializer)
    test:test("configs", test_configs, serializer)
    test:test("other", test_other, serializer)
    test:test("decode_buffer", common.test_decode_buffer, serializer)
end)
