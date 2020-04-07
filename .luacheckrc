include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "build/**/*.lua",
    "src/box/lua/serpent.lua", -- third-party source code
    "test/app/*.lua",
    "test/app-tap/lua/serializer_test.lua",
    "test/box/**/*.lua",
    "test/engine/*.lua",
    "test/engine_long/*.lua",
    "test/long_run-py/**/*.lua",
    "test/vinyl/*.lua",
    "test/replication/*.lua",
    "test/sql/*.lua",
    "test/swim/*.lua",
    "test/xlog/*.lua",
    "test/wal_off/*.lua",
    "test/var/**/*.lua",
    "test-run/**/*.lua",
    "third_party/**/*.lua",
    ".rocks/**/*.lua",
    ".git/**/*.lua",
}

files["**/*.lua"] = {
    globals = {"box", "_TARANTOOL", "help", "tutorial"},
    ignore = {"212/self", "122"}
}
files["extra/dist/tarantoolctl.in"] = {ignore = {"212/self", "122", "431"}}
files["src/lua/swim.lua"] = {ignore = {"431"}}
files["src/lua/fio.lua"] = {ignore = {"231"}}
files["src/lua/init.lua"] = {globals = {"dostring"}}
files["src/box/lua/console.lua"] = {ignore = {"212"}}
files["src/box/lua/load_cfg.lua"] = {ignore = {"542"}}
files["src/box/lua/net_box.lua"] = {ignore = {"431", "432", "231", "411", "212"}}
files["src/box/lua/schema.lua"] = {ignore = {"431", "432", "542", "212"}}
