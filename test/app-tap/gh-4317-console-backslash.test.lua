#!/usr/bin/env tarantool

local tap = require('tap')
local console = require('console')
local fio = require('fio')
local socket = require('socket')
local log = require('log')

--
-- gh-4317: add support of "/" at the end of line
--

-- The following cases use LD_PRELOAD, so will not work under
-- Mac OS X.
if jit.os == 'OSX' then
    os.exit(0)
end

test = tap.test("console")
test:plan(2)

--
-- Test local console with the help of isatty lib.
--
-- Locate libisatty.so related to the running tarantool binary.
local binary = arg[-1]
local isatty_lib_path = binary:gsub('tarantool$', '../test/libisatty.so')
assert(isatty_lib_path:match('libisatty.so$'), 'Failed to locate libisatty.so')
local tarantool_command = "local a = 0 \\\nfor i = 1, 10 do\na = a + i\nend \\\nprint(a)"
local result_str =  [[tarantool> local a = 0 \
         > for i = 1, 10 do
         > a = a + i
         > end \
         > print(a)
55
---
...

tarantool> ]]
local cmd = ([[printf '%s\n' | LD_PRELOAD='%s' tarantool ]] ..
[[2>/dev/null]]):format(tarantool_command, isatty_lib_path)
local fh = io.popen(cmd, 'r')
-- Readline on CentOS 7 produces \e[?1034h escape
-- sequence before tarantool> prompt, remove it.
local result = fh:read('*a'):gsub('\x1b%[%?1034h', '')
fh:close()
test:is(result, result_str, "backslash local")

--
-- Test for the remote console
--

-- Suppress console log messages
log.level(4)
local CONSOLE_SOCKET = fio.pathjoin(fio.cwd(), 'tarantool-test-console.sock')
local IPROTO_SOCKET = fio.pathjoin(fio.cwd(), 'tarantool-test-iproto.sock')
local EOL = "\n...\n"
local server = console.listen(CONSOLE_SOCKET)
local client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
local cmd = 'local a = 0 \\\nfor i = 1, 10 do\\\n a = a + i \\\n end \\\n print(a)'

client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
client:read(128)
client:write(('%s\n'):format(cmd))
result = client:read(EOL)
test:ok(string.find(result, 'error') == nil, 'backslash remote')
client:close()
os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)

os.exit(test:check() and 0 or 1)
