std = "luajit"
globals = {"box", "_TARANTOOL", "tonumber64"}
ignore = {
    -- Accessing an undefined field of a global variable <debug>.
    "143/debug",
    -- Accessing an undefined field of a global variable <os>.
    "143/os",
    -- Accessing an undefined field of a global variable <string>.
    "143/string",
    -- Accessing an undefined field of a global variable <table>.
    "143/table",
    -- Unused argument <self>.
    "212/self",
    -- Redefining a local variable.
    "411",
    -- Redefining an argument.
    "412",
    -- Shadowing a local variable.
    "421",
    -- Shadowing an upvalue.
    "431",
    -- Shadowing an upvalue argument.
    "432",
}

include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "build/**/*.lua",
    -- Third-party source code.
    "src/box/lua/serpent.lua",
    "test-run/**/*.lua",
    "test/app/*.test.lua",
    "test/box/*.test.lua",
    -- Unused source file, to be dropped (gh-5169).
    "test/box/lua/require_init.lua",
    -- Unused source file, to be dropped (gh-5169).
    "test/box/lua/require_mod.lua",
    -- Unused source file, to be dropped (gh-5169).
    "test/box/lua/test_init.lua",
    "test/engine/*.test.lua",
    "test/engine_long/*.test.lua",
    "test/replication/*.test.lua",
    "test/replication-py/**/*.lua",
    "test/sql-tap/**/*.lua",
    "test/sql/**/*.lua",
    "test/swim/**/*.lua",
    "test/var/**/*.lua",
    "test/vinyl/**/*.lua",
    "test/wal_off/*.lua",
    "test/xlog/**/*.lua",
    "test/xlog-py/**/*.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["extra/dist/tarantoolctl.in"] = {
    ignore = {
        -- https://github.com/tarantool/tarantool/issues/4929
        "122",
    },
}
files["src/lua/help.lua"] = {
    -- Globals defined for interactive mode.
    globals = {"help", "tutorial"},
}
files["src/lua/init.lua"] = {
    -- Miscellaneous global function definition.
    globals = {"dostring"},
}
files["src/box/lua/console.lua"] = {
    ignore = {
        -- https://github.com/tarantool/tarantool/issues/5032
        "212",
    }
}
files["test/app-tap/lua/require_mod.lua"] = {
    globals = {"exports"}
}
files["test/app-tap/string.test.lua"] = {
    globals = {"utf8"}
}
files["test/app/lua/fiber.lua"] = {
    globals = {"box_fiber_run_test"}
}
files["test/box/box.lua"] = {
    globals = {
        "cfg_filter",
        "sorted",
        "iproto_request",
    }
}
files["test/box/lua/push.lua"] = {
    globals = {"push_collection"}
}
files["test/box/lua/index_random_test.lua"] = {
    globals = {"index_random_test"}
}
files["test/box/lua/utils.lua"] = {
    globals = {
        "arithmetic",
        "check_space",
        "create_iterator",
        "iterate",
        "setmap",
        "sort",
        "space_field_types",
        "space_bsize",
        "table_generate",
        "table_shuffle",
        "tuple_to_string",
    }
}
files["test/box/lua/bitset.lua"] = {
    globals = {
        "clear",
        "create_space",
        "delete",
        "drop_space",
        "dump",
        "fill",
        "test_insert_delete",
    }
}
files["test/box/lua/fifo.lua"] = {
    globals = {
        "fifomax",
        "fifo_push",
        "fifo_top",
        "find_or_create_fifo",
    }
}
files["test/box/lua/identifier.lua"] = {
     globals = {"run_test"}
}
files["test/box-tap/session.test.lua"] = {
    globals = {
        "active_connections",
        "session",
        "space",
        "f1",
        "f2",
    },
}
files["test/box-tap/extended_error.test.lua"] = {
    globals = {
        "error_access_denied",
        "error_new",
        "error_new_stacked",
        "error_throw",
        "error_throw_stacked",
        "error_throw_access_denied",
        "forbidden_function",
    },
}
files["test/engine/conflict.lua"] = {
    globals = {
        "test_conflict",
    },
}
files["test/engine_long/suite.lua"] = {
    globals = {
        "delete_replace_update",
        "delete_insert",
    }
}
files["test/long_run-py/suite.lua"] = {
    globals = {
        "delete_replace_update",
        "delete_insert",
    }
}
files["test/replication/replica_quorum.lua"] = {
    globals = {
        "INSTANCE_URI",
        "nonexistent_uri",
    }
}
files["test/replication/replica_on_schema_init.lua"] = {
    globals = {
        "trig_local",
        "trig_engine",
    }
}
files["test/replication/lua/fast_replica.lua"] = {
    globals = {
        "call_all",
        "delete",
        "drop",
        "drop_all",
        "join",
        "start",
        "start_all",
        "stop",
        "stop_all",
        "vclock_diff",
        "unregister",
        "wait",
        "wait_all",
    },
}
