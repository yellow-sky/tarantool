include_files = {
    "**/*.lua",
    "extra/dist/tarantoolctl.in",
}

exclude_files = {
    "build/**/*.lua",
    "test/app/*.lua",
    "test/app-tap/lua/serializer_test.lua",
    "test/box/**/*.lua",
    "test/engine/*.lua",
    "test/engine_long/*.lua",
    "test/long_run-py/**/*.lua",
    "test/replication/*.lua",
    "test/sql/*.lua",
    "test/swim/*.lua",
    "test/var/**/*.lua",
    "test/vinyl/*.lua",
    "test/wal_off/*.lua",
    "test/xlog/*.lua",
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
