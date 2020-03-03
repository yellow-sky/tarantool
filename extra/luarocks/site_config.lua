local site_config = {}
site_config.LUAROCKS_PREFIX=[[/usr/local]]
site_config.LUA_INCDIR=[[/usr/local/include/tarantool]]
site_config.LUA_BINDIR=[[/usr/local/bin]]
site_config.LUA_INTERPRETER=[[tarantool]]
site_config.LUA_MODULES_LIB_SUBDIR=[[/lib/tarantool]]
site_config.LUA_MODULES_LUA_SUBDIR=[[/share/tarantool]]
site_config.LUAROCKS_SYSCONFDIR=[[/usr/local/etc/tarantool/rocks]]
site_config.LUAROCKS_FORCE_CONFIG=true
site_config.LUAROCKS_ROCKS_TREE=[[/usr/local/]]
site_config.LUAROCKS_ROCKS_SUBDIR=[[/share/tarantool/rocks]]
site_config.LUAROCKS_ROCKS_SERVERS={
    [[http://rocks.tarantool.org/]]
};
site_config.LUAROCKS_LOCALDIR = require('fio').cwd()
site_config.LUAROCKS_HOME_TREE_SUBDIR=[[/.rocks]]
site_config.LUA_DIR_SET=true
site_config.LUAROCKS_UNAME_S=[[Linux]]
site_config.LUAROCKS_UNAME_M=[[x86_64]]
site_config.LUAROCKS_DOWNLOADER=[[curl]]
site_config.LUAROCKS_MD5CHECKER=[[openssl]]
site_config.LUAROCKS_EXTERNAL_DEPS_SUBDIRS={ bin="bin", lib={ "lib", [[lib64]] }, include="include" }
site_config.LUAROCKS_RUNTIME_EXTERNAL_DEPS_SUBDIRS={ bin="bin", lib={ "lib", [[lib64]] }, include="include" }
site_config.LUAROCKS_LOCAL_BY_DEFAULT = true
return site_config
