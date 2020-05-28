include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "build/**/*.lua",
    "src/box/lua/serpent.lua", -- third-party source code
    "test/app/*.test.lua",
    "test/box/**/*.lua",
    "test/engine/*.test.lua",
    "test/engine_long/*.lua",
    "test/long_run-py/**/*.lua",
    "test/replication/*.test.lua",
    "test/sql/*.test.lua",
    "test/swim/*.test.lua",
    "test/vinyl/*.test.lua",
    "test/var/**/*.lua",
    "test/wal_off/*.test.lua",
    "test/xlog/*.test.lua",
    "test-run/**/*.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["extra/dist/tarantoolctl.in"] = {
    globals = {"box", "_TARANTOOL"},
    ignore = {"212/self", "122", "431"}
}
files["**/*.lua"] = {
    globals = {"box", "_TARANTOOL"},
    ignore = {"212/self", "143"}
}
files["src/lua/help.lua"] = {globals = {"help", "tutorial"}}
files["src/lua/init.lua"] = {globals = {"dostring", "os", "package"}}
files["src/lua/swim.lua"] = {ignore = {"431"}}
files["src/box/lua/console.lua"] = {globals = {"help"}, ignore = {"212"}}
files["src/box/lua/net_box.lua"] = {ignore = {"431", "432"}}
files["src/box/lua/schema.lua"] = {globals = {"tonumber64"}, ignore = {"431", "432"}}
files["test/app/lua/fiber.lua"] = {globals = {"box_fiber_run_test"}}
files["test/app-tap/lua/require_mod.lua"] = {globals = {"exports"}}
files["test/app-tap/module_api.test.lua"] = {ignore = {"311"}}
files["test/app-tap/string.test.lua"] = {globals = {"utf8"}}
files["test/box-tap/session.test.lua"] = {
	globals = {"active_connections", "session", "space", "f1", "f2"},
	ignore = {"211"}
}
files["test/box-tap/extended_error.test.lua"] = {
	globals = {"error_new", "error_throw", "error_new_stacked", "error_throw_stacked",
	"error_access_denied", "error_throw_access_denied", "forbidden_function"},
	ignore = {"211"}
}
files["test/box/lua/push.lua"] = {globals = {"push_collection"}}
files["test/box/lua/index_random_test.lua"] = {globals = {"index_random_test"}}
files["test/box/lua/utils.lua"] = {
	globals = {"space_field_types", "iterate", "arithmetic", "table_shuffle",
	"table_generate", "tuple_to_string", "check_space", "space_bsize",
	"create_iterator", "setmap", "sort"}}
files["test/box/lua/bitset.lua"] = {
	globals = {"create_space", "fill", "delete", "clear", "drop_space",
	"dump", "test_insert_delete"}
}
files["test/box/lua/fifo.lua"] = {globals = {"fifomax", "find_or_create_fifo", "fifo_push", "fifo_top"}}
files["test/box/lua/identifier.lua"] = {globals = {"run_test"}}
files["test/box/lua/require_mod.lua"] = {globals = {"exports"}}
files["test/engine/conflict.lua"] = {globals = {"test_conflict"}}
files["test/replication/replica_quorum.lua"] = {globals = {"INSTANCE_URI", "nonexistent_uri"}}
files["test/replication/replica_on_schema_init.lua"] = {globals = {"trig_local", "trig_engine"}}
files["test/replication/lua/fast_replica.lua"] = {
	globals = {"join", "start_all", "stop_all", "wait_all",
	"drop_all", "drop_all", "vclock_diff", "unregister",
	"delete", "start", "stop", "call_all", "drop", "wait"},
	ignore = {"212", "213"}
}
files["test/sql-tap/*.lua"] = {ignore = {"111", "113", "211", "611", "612", "613", "614", "621", "631"}}
files["test/sql-tap/lua/sqltester.lua"] = {globals = {"table_match_regex_p"}}
files["test/sql-tap/e_expr.test.lua"] = {ignore = {"512"}}
files["test/swim/box.lua"] = {globals = {"listen_port", "listen_uri", "uuid", "uri", "swim", "fiber"}}
