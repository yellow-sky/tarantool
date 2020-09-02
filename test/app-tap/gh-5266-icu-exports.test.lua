#!/usr/bin/env tarantool

local tap = require('tap')
local ffi = require('ffi')

local test = tap.test('icu_exports')
test:plan(2)

-- Find `u_getVersion` symbol (by nm) from tarantool binary
-- for further detection of icu version. There are two approaches:
-- * some of icu packages appends icu version to it's symbols as suffix,
--   so we need to simple parse it.
-- * other packages don't append version as suffix, so in this case
--   we have to call u_getVersion through ffi to grab icu version.
-- On Freebsd nm requires -D option, while on Linux it's not necessary
-- and macOS nm doesn't have this option.
local tnt_path = arg[-1]
local pipe = io.popen(string.format(
    'nm %s %s | grep u_getVersion',
    jit.os == "BSD" and '-D' or '',
    tnt_path
))
local u_strlen_info = pipe:read('*all')
pipe:close()

test:ok(u_strlen_info ~= '', "Found u_getVersion symbol (to get icu version)")
local icu_version_suffix = u_strlen_info:gsub('.*u_getVersion', ''):gsub('\n', '')

local icu_version_major = tonumber(icu_version_suffix:gsub('_', ''), 10)
if icu_version_suffix == '' then
    ffi.cdef([[
        typedef uint8_t UVersionInfo[4];
        void u_getVersion(UVersionInfo versionArray);
    ]])
    local version = ffi.new('UVersionInfo')
    ffi.C.u_getVersion(version)
    icu_version_major = version[0]
end

ffi.cdef([[
    void *dlsym(void *handle, const char *symbol);
]])

-- See `man 3 dlsym`:
-- RTLD_DEFAULT
--   Find the first occurrence of the desired symbol using the default
--   shared object search order. The search will include global symbols
--   in the executable and its dependencies, as well as symbols in shared
--   objects that were dynamically loaded with the RTLD_GLOBAL flag.
local RTLD_DEFAULT = ffi.cast("void *",
    (jit.os == "OSX" or jit.os == "BSD") and -2LL or 0LL
)

local icu_symbols = {
    'u_strlen',
    'u_uastrcpy',
    'u_austrcpy',
    'u_errorName',
    'u_getVersion',
    'udat_open',
    'udat_setLenient',
    'udat_close',
    'udat_parseCalendar',
    'ucal_open',
    'ucal_close',
    'ucal_get',
    'ucal_set',
    'ucal_add',
    'ucal_clear',
    'ucal_clearField',
    'ucal_getMillis',
    'ucal_setMillis',
    'ucal_getAttribute',
    'ucal_setAttribute',
    'ucal_setTimeZone',
    'ucal_getNow',
}

if icu_version_major >= 51 then
    table.insert(icu_symbols, 'ucal_getTimeZoneID')
end
if icu_version_major >= 55 then
    table.insert(icu_symbols, 'udat_formatCalendar')
end

test:test('icu_symbols', function(t)
    t:plan(#icu_symbols)
    for _, sym in ipairs(icu_symbols) do
        local version_sym = sym .. icu_version_suffix
        t:ok(
            ffi.C.dlsym(RTLD_DEFAULT, version_sym) ~= nil,
            ('Symbol %q found'):format(version_sym)
        )
    end
end)

os.exit(test:check() and 0 or 1)
