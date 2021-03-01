-- Disable strict for Tarantool.
require("strict").off()

-- Add strict off to `progname` command, that runs child tests
-- in PUC-Rio and lua-Harness test suite to disable strict there
-- too. Quotes type is important.
-- luacheck thinks that `arg` is read-only global variable.
-- This is not true.
-- luacheck: no global
arg[-1] = ('%s -e "require[[strict]].off()"'):format(arg[-1])
