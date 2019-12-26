test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

-- There is txn inside this function
box.schema.func.create('F', { language = 'LUA', \
    body = 'function () local m = box.space._space:select() return 0 end', \
    exports = {'LUA', 'SQL'}})

t = box.execute('CREATE TABLE t (i INT PRIMARY KEY);')
i = box.execute('INSERT INTO t VALUES (1), (2), (3);')
--
-- As long as SELECT is executed inside transaction,
-- the assertion mentioned in gh-4504 should not fail
--
res = box.execute('WITH RECURSIVE w AS (SELECT i FROM t UNION ALL \
		  SELECT i + 1  FROM w WHERE i < 4) SELECT F() FROM w;')

box.func.F:drop()
