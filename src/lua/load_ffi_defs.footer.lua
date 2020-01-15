
]]

-- {{{ Definitions

-- POSIX demands to have five fields in struct group:
-- char    *pw_name   User's login name.
-- uid_t    pw_uid    Numerical user ID.
-- gid_t    pw_gid    Numerical group ID.
-- char    *pw_dir    Initial working directory.
-- char    *pw_shell  Program to use as shell.
--
-- So we'll extract only them.
if ffi.os == 'OSX' or ffi.os == 'BSD' then
    ffi.cdef[[
        struct passwd {
            char    *pw_name;    /* user name */
            char    *pw_passwd;  /* encrypted password */
            uid_t    pw_uid;     /* user uid */
            gid_t    pw_gid;     /* user gid */
            time_t   pw_change;  /* password change time */
            char    *pw_class;   /* user access class */
            char    *pw_gecos;   /* Honeywell login info */
            char    *pw_dir;     /* home directory */
            char    *pw_shell;   /* default shell */
            time_t   pw_expire;  /* account expiration */
            int      pw_fields;  /* internal: fields filled in */
        };
    ]]
else
    ffi.cdef[[
        struct passwd {
            char *pw_name;   /* username */
            char *pw_passwd; /* user password */
            int   pw_uid;    /* user ID */
            int   pw_gid;    /* group ID */
            char *pw_gecos;  /* user information */
            char *pw_dir;    /* home directory */
            char *pw_shell;  /* shell program */
        };
    ]]
end

-- }}}

ffi.cdef[[
    /* From src/lib/core/diag.h
     * This struct is here because src/lua/error.lua
     * uses underscored member names
     * (i.e. we cannot use 'type' member since it's a keyword)
     */
    struct error {
        error_f _destroy;
        error_f _raise;
        error_f _log;
        const struct type_info *_type;
        int _refs;
        int _saved_errno;
        /** Line number. */
        unsigned _line;
        /* Source file name. */
        char _file[DIAG_FILENAME_MAX];
        /* Error description. */
        char _errmsg[DIAG_ERRMSG_MAX];
    };

    /* From src/box/user_def.h
     * This enum is here because Lua C parser
     * incorrecly assigns PRIV_ALL = -1
     * - this is signed type, and unsigned is required
     */
    enum priv_type {
        /* SELECT */
        PRIV_R = 1,
        /* INSERT, UPDATE, UPSERT, DELETE, REPLACE */
        PRIV_W = 2,
        /* CALL */
        PRIV_X = 4,
        /* SESSION */
        PRIV_S = 8,
        /* USAGE */
        PRIV_U = 16,
        /* CREATE */
        PRIV_C = 32,
        /* DROP */
        PRIV_D = 64,
        /* ALTER */
        PRIV_A = 128,
        /* REFERENCE - required by ANSI - not implemented */
        PRIV_REFERENCE = 256,
        /* TRIGGER - required by ANSI - not implemented */
        PRIV_TRIGGER = 512,
        /* INSERT - required by ANSI - not implemented */
        PRIV_INSERT = 1024,
        /* UPDATE - required by ANSI - not implemented */
        PRIV_UPDATE = 2048,
        /* DELETE - required by ANSI - not implemented */
        PRIV_DELETE = 4096,
        /* This is never granted, but used internally. */
        PRIV_GRANT = 8192,
        /* Never granted, but used internally. */
        PRIV_REVOKE = 16384,
        /* all bits */
        PRIV_ALL  = 4294967295
    };
]]

ffi.cdef[[
    /* From src/lib/small/ibuf.h (submodule) */

    struct ibuf
    {
        struct slab_cache *slabc;
        char *buf;
        /** Start of input. */
        char *rpos;
        /** End of useful input */
        char *wpos;
        /** End of ibuf. */
        char *epos;
        size_t start_capacity;
    };

    void
    ibuf_create(struct ibuf *ibuf, struct slab_cache *slabc, size_t start_capacity);

    void
    ibuf_destroy(struct ibuf *ibuf);

    void
    ibuf_reinit(struct ibuf *ibuf);

    void *
    ibuf_reserve_slow(struct ibuf *ibuf, size_t size);
]]

ffi.cdef[[
    /* From src/lib/msgpuck/msgpuck.h (submodule) */

    uint32_t
    mp_decode_extl(const char **data, int8_t *type);

    char *
    mp_encode_float(char *data, float num);

    char *
    mp_encode_double(char *data, double num);

    float
    mp_decode_float(const char **data);

    double
    mp_decode_double(const char **data);
]]

