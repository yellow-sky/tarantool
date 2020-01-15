local ffi   = require('ffi')
local errno = require('errno')

-- {{{ Error handling

local pwgr_errstr = "get%s failed [errno %d]: %s"

-- Use it in the following way: set errno to zero, call a passwd /
-- group function, then call this function to check whether there
-- was an error.
local function pwgrcheck(func_name, pwgr)
    if pwgr ~= nil then
        return
    end
    if errno() == 0 then
        return
    end
    error(pwgr_errstr:format(func_name, errno(), errno.strerror()))
end

-- }}}

-- {{{ Call passwd / group database

local function _getpw(uid)
    local pw = nil
    errno(0)
    if type(uid) == 'number' then
        pw = ffi.C.getpwuid(uid)
    elseif type(uid) == 'string' then
        pw = ffi.C.getpwnam(uid)
    else
        error("Bad type of uid (expected 'string'/'number')")
    end
    pwgrcheck('_getpw', pw)
    return pw
end

local function _getgr(gid)
    local gr = nil
    errno(0)
    if type(gid) == 'number' then
        gr = ffi.C.getgrgid(gid)
    elseif type(gid) == 'string' then
        gr = ffi.C.getgrnam(gid)
    else
        error("Bad type of gid (expected 'string'/'number')")
    end
    pwgrcheck('_getgr', gr)
    return gr
end

-- }}}

-- {{{ Serialize passwd / group structures to tables

local function grtotable(gr)
    local gr_mem, group_members = gr.gr_mem, {}
    local i = 0
    while true do
        local member = gr_mem[i]
        if member == nil then
            break
        end
        table.insert(group_members, ffi.string(member))
        i = i + 1
    end
    return {
        id      = tonumber(gr.gr_gid),
        name    = ffi.string(gr.gr_name),
        members = group_members,
    }
end

-- gr is optional
local function pwtotable(pw, gr)
    local user = {
        name    = ffi.string(pw.pw_name),
        id      = tonumber(pw.pw_uid),
        workdir = ffi.string(pw.pw_dir),
        shell   = ffi.string(pw.pw_shell),
    }
    if gr ~= nil then
        user.group = grtotable(gr)
    end
    return user
end

-- }}}

-- {{{ Public API functions

local function getgr(gid)
    if gid == nil then
        gid = tonumber(ffi.C.getgid())
    end
    local gr = _getgr(gid)
    if gr == nil then
        return nil
    end
    return grtotable(gr)
end

local function getpw(uid)
    if uid == nil then
        uid = tonumber(ffi.C.getuid())
    end
    local pw = _getpw(uid)
    if pw == nil then
        return nil
    end
    local gr = _getgr(pw.pw_gid) -- can be nil
    return pwtotable(pw, gr)
end

local function getpwall()
    ffi.C.setpwent()
    local pws = {}
    -- Note: Don't call _getpw() during the loop: it leads to drop
    -- of a getpwent() current entry to a first one on CentOS 6
    -- and FreeBSD 12.
    while true do
        errno(0)
        local pw = ffi.C.getpwent()
        if pw == nil then
            pwgrcheck('getpwall', pw)
            break
        end
        local gr = _getgr(pw.pw_gid) -- can be nil
        table.insert(pws, pwtotable(pw, gr))
    end
    ffi.C.endpwent()
    return pws
end

local function getgrall()
    ffi.C.setgrent()
    local grs = {}
    -- Note: Don't call _getgr() during the loop: it leads to drop
    -- of a getgrent() current entry to a first one on CentOS 6
    -- and FreeBSD 12.
    while true do
        errno(0)
        local gr = ffi.C.getgrent()
        if gr == nil then
            pwgrcheck('getgrall', gr)
            break
        end
        table.insert(grs, grtotable(gr))
    end
    ffi.C.endgrent()
    return grs
end

-- }}}

-- Workaround pwd.getpwall() issue on Fedora 29: successful
-- getgrent() call that should normally return NULL and preserve
-- errno, set it to ENOENT due to systemd-nss issue [1] when a
-- password database is traversed first time.
--
-- [1]: https://github.com/systemd/systemd/issues/9585
pcall(getpwall)

return {
    getpw = getpw,
    getgr = getgr,
    getpwall = getpwall,
    getgrall = getgrall,
}
