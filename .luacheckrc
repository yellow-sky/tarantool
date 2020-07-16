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
    "test/box-tap/**/*.lua",
    "test/engine/**/*.lua",
    "test/engine_long/*.lua",
    "test/long_run-py/**/*.lua",
    "test/replication/**/*.lua",
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
