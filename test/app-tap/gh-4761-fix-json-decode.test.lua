#!/usr/bin/env tarantool

local tap = require('tap')
tap.test("json", function(test)
    local serializer = require('json')
    test:plan(4)

    --
    -- gh-4761: json.decode silently changes instances settings when called
    -- with 2nd parameter.
    --
    test:ok(not pcall(serializer.decode, '{"foo":{"bar": 1}}',
                      {decode_max_depth = 1}),
            'error: too many nested data structures')
    test:ok(pcall(serializer.decode, '{"foo":{"bar": 1}}'),
            'json instance settings are unchanged')

    --
    -- Same check for json.encode.
    --
    local nan = 1/0
    test:ok(not pcall(serializer.encode, {a = 1/0},
                      {encode_invalid_numbers = false}),
            'expected error with NaN encoding with .encode')
    test:ok(pcall(serializer.encode, {a=nan}),
            "json instance settings are unchanged")

end)
