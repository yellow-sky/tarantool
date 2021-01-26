#!/usr/bin/env tarantool

local fio = require('fio')

-- Use BUILDDIR passed from test-run or cwd when run w/o
-- test-run to find h20/h2o.{so,dylib}.
local build_path = os.getenv('BUILDDIR') or '.'
package.cpath = fio.pathjoin(build_path, 'h2o/?.so'   ) .. ';' ..
                fio.pathjoin(build_path, 'h2o/?.dylib') .. ';' ..
                package.cpath

box.cfg{
  listen = 3306,
}

local s = box.schema.space.create('tester')
s:format({
             {name = 'id', type = 'unsigned'},
             {name = 'desc', type = 'string'},
         })
local index = s:create_index('primary',
        {
            type = 'tree',
            parts = {'id'}
        })

s:insert{1, 'First'}
s:insert{2, 'Second'}

local h2o_lib = require "h2o"
local init_func = h2o_lib.init;

init_func()
