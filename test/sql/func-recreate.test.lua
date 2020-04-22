test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- Check errors during function create process
test_run:cmd("setopt delimiter ';'")
box.schema.func.create('WAITFOR', {language = 'Lua',
                       body = 'function (n) return n end',
                       param_list = {'number'}, returns = 'number',
                       exports = {'LUA', 'SQL'}});
box.schema.func.create('WAITFOR', {language = 'Lua',
                       body = 'function (n) return n end',
                       param_list = {'number'}, returns = 'number',
                       exports = {'LUA', 'SQL'}});
test_run:cmd("setopt delimiter ''");

box.func.WAITFOR:drop()
