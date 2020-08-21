#!/usr/bin/env tarantool

local tap = require('tap')
local ffi = require('ffi')
ffi.cdef([[
    void *dlsym(void *handle, const char *symbol);
    /**
     * Lexer for JSON paths:
     * <field>, <.field>, <[123]>, <['field']> and their combinations.
     */
    struct json_lexer {
        /** Source string. */
        const char *src;
        /** Length of string. */
        int src_len;
        /** Current lexer's offset in bytes. */
        int offset;
        /** Current lexer's offset in symbols. */
        int symbol_count;
        /**
         * Base field offset for emitted JSON_TOKEN_NUM tokens,
         * e.g. 0 for C and 1 for Lua.
         */
        int index_base;
    };

    enum json_token_type {
        JSON_TOKEN_NUM,
        JSON_TOKEN_STR,
        JSON_TOKEN_ANY,
        /** Lexer reached end of path. */
        JSON_TOKEN_END,
    };

    int
    json_lexer_next_token(struct json_lexer *lexer, struct json_token *token);

    int
    json_path_cmp(const char *a, int a_len, const char *b, int b_len,
            int index_base);

    int
    json_path_validate(const char *path, int path_len, int index_base);

    int
    json_path_multikey_offset(const char *path, int path_len, int index_base);
]])

local test = tap.test('json-features')
test:plan(1)

local RTLD_DEFAULT
-- See `man 3 dlsym`:
-- RTLD_DEFAULT
--   Find  the  first occurrence of the desired symbol using the default
--   shared object search order.  The search will include global symbols
--   in the executable and its dependencies, as well as symbols in shared
--   objects that were dynamically loaded with the RTLD_GLOBAL flag.
if jit.os == "OSX" then
    RTLD_DEFAULT = ffi.cast("void *", -2LL)
else
    RTLD_DEFAULT = ffi.cast("void *", 0LL)
end

local json_symbols = {
    'json_lexer_next_token',
    'json_path_cmp',
    'json_path_validate',
    'json_path_multikey_offset',
}

test:test('json_symbols', function(t)
    t:plan(#json_symbols)
    for _, sym in ipairs(json_symbols) do
        t:ok(
            ffi.C.dlsym(RTLD_DEFAULT, sym) ~= nil,
            ('Symbol %q found'):format(sym)
        )
    end
end)

os.exit(test:check() and 0 or 1)
