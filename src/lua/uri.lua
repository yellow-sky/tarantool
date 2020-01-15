-- uri.lua (internal file)

local ffi = require('ffi')
local static_alloc = require('buffer').static_alloc

local builtin = ffi.C;

local function parse(str)
    if str == nil then
        error("Usage: uri.parse(string)")
    end
    local uribuf = static_alloc('struct uri')
    if builtin.uri_parse(uribuf, str) ~= 0 then
        return nil
    end
    local result = {}
    for _, k in ipairs({ 'scheme', 'login', 'password', 'host', 'service',
        'path', 'query', 'fragment'}) do
        if uribuf[k] ~= nil then
            result[k] = ffi.string(uribuf[k], uribuf[k..'_len'])
        end
    end
    if uribuf.host_hint == 1 then
        result.ipv4 = result.host
    elseif uribuf.host_hint == 2 then
        result.ipv6 = result.host
    elseif uribuf.host_hint == 3 then
        result.unix = result.service
    end
    return result
end

local function format(uri, write_password)
    local uribuf = static_alloc('struct uri')
    uribuf.scheme = uri.scheme
    uribuf.scheme_len = string.len(uri.scheme or '')
    uribuf.login = uri.login
    uribuf.login_len = string.len(uri.login or '')
    uribuf.password = uri.password
    uribuf.password_len = string.len(uri.password or '')
    uribuf.host = uri.host
    uribuf.host_len = string.len(uri.host or '')
    uribuf.service = uri.service
    uribuf.service_len = string.len(uri.service or '')
    uribuf.path = uri.path
    uribuf.path_len = string.len(uri.path or '')
    uribuf.query = uri.query
    uribuf.query_len = string.len(uri.query or '')
    uribuf.fragment = uri.fragment
    uribuf.fragment_len = string.len(uri.fragment or '')
    local str = static_alloc('char', 1024)
    builtin.uri_format(str, 1024, uribuf, write_password and 1 or 0)
    return ffi.string(str)
end

return {
    parse = parse,
    format = format,
};
