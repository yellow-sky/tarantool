include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "third_party/**/*.lua",
    "test/wal_off/*.lua", -- E011
    "test/xlog/*.lua", -- E011
    "test/vinyl/*.lua", -- E011
    "test/sql/*.lua", -- E011
    "test/replication/*.lua", -- E011
    "test/engine/*.lua", -- E011
    "test/swim/*.lua", -- E011
    "test/box/*.lua", -- E011
    "test/app/*.lua", -- E011
    "test/app-tap/*.lua", -- E011
    "test/app-tap/lua/serializer_test.lua", -- E011
    "test-run/lib/tarantool-python/test-run/**/*.lua",
    "build/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["**/*.lua"] = {ignore = {"212", "122"}}
files["extra/dist/tarantoolctl.in"] = {ignore = {"431", "411", "122", "542", "212"}}
files["src/lua/help.lua"] = {globals = {"help", "tutorial"}, ignore = {"113", "211"}}
files["src/lua/swim.lua"] = {ignore = {"431"}}
files["src/lua/fio.lua"] = {ignore = {"231"}}
files["src/lua/init.lua"] = {ignore = {"122", "121"}}
files["src/lua/fiber.lua"] = {ignore = {"331", "311"}}
files["src/box/lua/serpent.lua"] = {globals = {"_ENV"}, ignore = {"431", "421", "631", "432", "542", "412", "422"}}
files["src/box/lua/load_cfg.lua"] = {ignore = {"431", "143", "311", "542"}}
files["src/box/lua/net_box.lua"] = {ignore = {"431", "211", "421", "432", "311", "231", "411", "412"}}
files["src/box/lua/schema.lua"] = {globals = {"update_format", "iid", "role_check_grant_revoke_of_sys_priv"}, ignore = {"431", "122", "211", "421", "213", "432", "311", "542", "412"}}
files["src/box/lua/key_def.lua"] = {ignore = {"431"}}
files["src/box/lua/feedback_daemon.lua"] = {ignore = {"122", "211"}}
files["src/box/lua/tuple.lua"] = {ignore = {"122", "211", "421", "412"}}
files["src/box/lua/upgrade.lua"] = {ignore = {"122", "211", "421", "213"}}
files["src/box/lua/console.lua"] = {globals = {"help"}, ignore = {"211", "143"}}
files["test-run/test_run.lua"] = {ignore = {"412", "211", "431"}}
files["test-run/lib/tarantool-python/test/cluster-py/master.lua"] = {ignore = {"121"}}
files["test-run/lib/tarantool-python/unit/suites/box.lua"] = {ignore = {"121"}}
files["test/**/*.lua"] = {ignore = {"631", "611", "614", "613", "612", "621", "112", "211", "432"}}
files["test/app/*.lua"] = {ignore = {"111", "113"}}
files["test/app/lua/fiber.lua"] = {ignore = {"111"}}
files["test/app-tap/*.lua"] = {ignore = {"111", "113"}}
files["test/app-tap/tarantoolctl.test.lua"] = {ignore = {"511"}}
files["test/app-tap/lua/require_mod.lua"] = {ignore = {"113", "111"}}
files["test/box/admin.test.lua"] = {ignore = {"213"}}
files["test/box/lua/*.lua"] = {ignore = {"111"}}
files["test/box/*.lua"] = {ignore = {"111", "113"}}
files["test/box/lua/utils.lua"] = {ignore = {"421", "113", "213", "412"}}
files["test/box/lua/bitset.lua"] = {ignore = {"113", "213"}}
files["test/box/lua/fifo.lua"] = {ignore = {"113", "213"}}
files["test/box/lua/identifier.lua"] = {ignore = {"113"}}
files["test/box/lua/require_mod.lua"] = {ignore = {"113"}}
files["test/box/lua/require_init.lua"] = {ignore = {"412", "143"}}
files["test/box/lua/test_init.lua"] = {ignore = {"113", "412", "143"}}
files["test/box/lua/index_random_test.lua"] = {ignore = {"213"}}
files["test/box-tap/*.lua"] = {ignore = {"111"}}
files["test/box-tap/auth.test.lua"] = {ignore = {"311", "411", "113"}}
files["test/box-tap/cfg.test.lua"] = {ignore = {"311", "411", "113"}}
files["test/box-tap/cfgup.test.lua"] = {ignore = {"113"}}
files["test/box-tap/feedback_daemon.test.lua"] = {ignore = {"411"}}
files["test/box-tap/gc.test.lua"] = {ignore = {"421"}}
files["test/box-tap/on_schema_init.test.lua"] = {ignore = {"113"}}
files["test/box-tap/schema_mt.test.lua"] = {ignore = {"113", "122"}}
files["test/box-tap/key_def.test.lua"] = {ignore = {"411", "431"}}
files["test/box-tap/merger.test.lua"] = {ignore = {"411", "431", "412", "213"}}
files["test/box-tap/session.storage.test.lua"] = {ignore = {"113"}}
files["test/box-tap/session.test.lua"] = {ignore = {"411", "113", "412", "143"}}
files["test/box-tap/trigger_atexit.test.lua"] = {ignore = {"113"}}
files["test/box-tap/trigger_yield.test.lua"] = {ignore = {"213", "113"}}
files["test/luajit-tap/fix_string_find_recording.test.lua"] = {ignore = {"111", "113", "231"}}
files["test/luajit-tap/gh-4476-fix-string-find-recording.test.lua"] = {ignore = {"231"}}
files["test/luajit-tap/fold_bug_LuaJIT_505.test.lua"] = {ignore = {"111", "113"}}
files["test/luajit-tap/gh.test.lua"] = {ignore = {"111", "113"}}
files["test/luajit-tap/table_chain_bug_LuaJIT_494.test.lua"] = {ignore = {"111", "113"}}
files["test/luajit-tap/unsink_64_kptr.test.lua"] = {ignore = {"111", "113", "551", "542"}}
files["test/luajit-tap/or-232-unsink-64-kptr.test.lua"] = {ignore = {"542"}}
files["test/luajit-tap/lj-494-table-chain-infinite-loop.test.lua"] = {ignore = {"111", "113", "213"}}
files["test/engine/*.lua"] = {ignore = {"111", "113"}}
files["test/engine_long/suite.lua"] = {ignore = {"421", "111", "213"}}
files["test/engine_long/delete_replace_update.test.lua"] = {ignore = {"113", "111"}}
files["test/engine_long/delete_insert.test.lua"] = {ignore = {"113", "111"}}
files["test/long_run-py/suite.lua"] = {ignore = {"421", "111", "113", "213"}}
files["test/long_run-py/lua/finalizers.lua"] = {ignore = {"241", "511", "111", "113"}}
files["test/replication/*.lua"] = {ignore = {"113", "111"}}
files["test/replication/lua/fast_replica.lua"] = {ignore = {"113", "213"}}
files["test/replication/lua/rlimit.lua"] = {ignore = {"113"}}
files["test/replication-py/replica.lua"] = {ignore = {"111"}}
files["test/replication/lua/fast_replica.lua"] = {ignore = {"113", "111", "213"}}
files["test/replication/lua/rlimit.lua"] = {ignore = {"113", "111"}}
files["test/sql/*.lua"] = {ignore = {"113", "111"}}
files["test/sql/triggers.test.lua"] = {ignore = {"631", "113", "111"}}
files["test/sql/tokenizer.test.lua"] = {ignore = {"113", "111"}}
files["test/sql/transitive-transactions.test.lua"] = {ignore = {"113", "111"}}
files["test/sql/savepoints.test.lua"] = {ignore = {"113", "111"}}
files["test/sql/persistency.test.lua"] = {ignore = {"113", "111"}}
files["test/sql-tap/*.lua"] = {ignore = {"113", "111"}}
files["test/sql-tap/lua_sql.test.lua"] = {ignore = {"412"}}
files["test/sql-tap/subquery.test.lua"] = {ignore = {"412", "143"}}
files["test/sql-tap/alias.test.lua"] = {ignore = {"412", "143"}}
files["test/sql-tap/analyze9.test.lua"] = {ignore = {"213", "411"}}
files["test/sql-tap/between.test.lua"] = {ignore = {"421", "213"}}
files["test/sql-tap/check.test.lua"] = {ignore = {"412", "143"}}
files["test/sql-tap/date.test.lua"] = {ignore = {"511"}}
files["test/sql-tap/e_expr.test.lua"] = {ignore = {"512", "431", "213", "411"}}
files["test/sql-tap/func.test.lua"] = {ignore = {"412", "143"}}
files["test/sql-tap/func5.test.lua"] = {ignore = {"412", "143"}}
files["test/sql-tap/misc1.test.lua"] = {ignore = {"411"}}
files["test/sql-tap/select1.test.lua"] = {ignore = {"511", "213"}}
files["test/sql-tap/select2.test.lua"] = {ignore = {"421"}}
files["test/sql-tap/select4.test.lua"] = {ignore = {"421"}}
files["test/sql-tap/select5.test.lua"] = {ignore = {"421"}}
files["test/sql-tap/selectA.test.lua"] = {ignore = {"542"}}
files["test/sql-tap/selectB.test.lua"] = {ignore = {"213", "542"}}
files["test/sql-tap/selectG.test.lua"] = {ignore = {"421"}}
files["test/sql-tap/table.test.lua"] = {ignore = {"511"}}
files["test/sql-tap/tkt-bd484a090c.test.lua"] = {ignore = {"511"}}
files["test/sql-tap/tkt-38cb5df375.test.lua"] = {ignore = {"421"}}
files["test/sql-tap/tkt-91e2e8ba6f.test.lua"] = {ignore = {"542"}}
files["test/sql-tap/tkt-9a8b09f8e6.test.lua"] = {ignore = {"542"}}
files["test/sql-tap/tkt2192.test.lua"] = {ignore = {"511"}}
files["test/sql-tap/tkt3493.test.lua"] = {ignore = {"542"}}
files["test/sql-tap/triggerA.test.lua"] = {ignore = {"311"}}
files["test/sql-tap/trigger2.test.lua"] = {ignore = {"213"}}
files["test/sql-tap/trigger9.test.lua"] = {ignore = {"143", "412"}}
files["test/sql-tap/update.test.lua"] = {ignore = {"611"}}
files["test/sql-tap/whereB.test.lua"] = {ignore = {"611"}}
files["test/sql-tap/with1.test.lua"] = {ignore = {"542"}}
files["test/sql-tap/with2.test.lua"] = {ignore = {"213", "412"}}
files["test/sql-tap/lua_sql.test.lua"] = {ignore = {"143"}}
files["test/sql-tap/lua/sqltester.lua"] = {ignore = {"111", "113", "431", "213"}}
files["test/sql-tap/gh-2723-concurrency.test.lua"] = {ignore = {"213"}}
files["test/sql-tap/gh-3307-xfer-optimization-issue.test.lua"] = {ignore = {"412", "213"}}
files["test/sql-tap/gh-3083-ephemeral-unref-tuples.test.lua"] = {ignore = {"213"}}
files["test/sql-tap/gh-3332-tuple-format-leak.test.lua"] = {ignore = {"213"}}
files["test/sql-tap/gh2127-indentifier-max-length.test.lua"] = {ignore = {"213"}}
files["test/sql-tap/misc5.test.lua"] = {ignore = {"213"}}
files["test/wal_off/rtree_benchmark.test.lua"] = {ignore = {"213", "113", "111"}}
files["test/wal_off/func_max.test.lua"] = {ignore = {"511", "113", "111"}}
files["test/vinyl/upgrade/fill.lua"] = {ignore = {"111", "113"}}
files["test/vinyl/write_iterator_rand.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/savepoint.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/replica_quota.test.lua"] = {ignore = {"113", "111", "213"}}
files["test/vinyl/stress.test.lua"] = {ignore = {"431", "213"}}
files["test/vinyl/hermitage.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/dump_stress.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/constraint.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/large.test.lua"] = {ignore = {"113", "111"}}
files["test/vinyl/options.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/upgrade/2.1.3/gh-4771-upgrade-sequence/fill.lua"] = {ignore = {"111", "113"}}
files["test/xlog/transaction.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/snap_io_rate.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/reader.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/panic_on_broken_lsn.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/gh1433.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/header.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/errinj.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/force_recovery.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/big_tx.test.lua"] = {ignore = {"113", "111"}}
files["test/xlog/checkpoint_threshold.test.lua"] = {ignore = {"113", "111", "213"}}
files["test/swim/box.lua"] = {ignore = {"113", "111"}}
