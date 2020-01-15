---
--- Unified Lua ffi.cdef builder header file.
--- The definitions below are either from system headers
--- or from dependent packages or tarantol repo files
--- which cannot be automatically preprocessed.
---

ffi = require('ffi')

ffi.cdef[[
/* from libc */

char *strerror(int errnum);
int snprintf(char *str, size_t size, const char *format, ...);
const char *memmem(const char *haystack, size_t haystack_len,
                   const char *needle, size_t needle_len);
int memcmp(const char *mem1, const char *mem2, size_t num);
int isspace(int c);
typedef int32_t pid_t;
pid_t getpid(void);

// GID_T, UID_T and TIME_T are, essentially, `integer types`.
// http://pubs.opengroup.org/onlinepubs/009695399/basedefs/sys/types.h.html
typedef int uid_t;
typedef int gid_t;
typedef long time_t;

// POSIX demands to have three fields in struct group:
// http://pubs.opengroup.org/onlinepubs/009695399/basedefs/grp.h.html
// char   *gr_name The name of the group.
// gid_t   gr_gid  Numerical group ID.
// char  **gr_mem  Pointer to a null-terminated array of character pointers to
//                 member names.
//
// So we'll extract only them.
struct group {
    char    *gr_name;    /* group name */
    char    *gr_passwd;  /* group password */
    gid_t    gr_gid;     /* group id */
    char   **gr_mem;     /* group members */
};

uid_t          getuid();
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *login);
void           endpwent();
struct passwd *getpwent();
void           setpwent();
gid_t          getgid();
struct group  *getgrgid(gid_t gid);
struct group  *getgrnam(const char *group);
struct group  *getgrent();
void           endgrent();
void           setgrent();

int umask(int mask);
char *dirname(char *path);
int chdir(const char *path);

struct gc_socket {
    const int fd;
};

typedef uint32_t socklen_t;
typedef ptrdiff_t ssize_t;
struct sockaddr;

int connect(int sockfd, const struct sockaddr *addr,
            socklen_t addrlen);
int bind(int sockfd, const struct sockaddr *addr,
         socklen_t addrlen);
ssize_t write(int fd, const char *octets, size_t len);
ssize_t read(int fd, void *buf, size_t count);
int listen(int fd, int backlog);
int socket(int domain, int type, int protocol);
int shutdown(int s, int how);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int s, void *buf, size_t len, int flags);
int accept(int s, void *addr, void *addrlen);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
int setsockopt(int s, int level, int iname, const void *opt, size_t optlen);
int getsockopt(int s, int level, int iname, void *ptr, size_t *optlen);

typedef struct { int active; int timeout; } linger_t;

struct protoent {
    char  *p_name;       /* official protocol name */
    char **p_aliases;    /* alias list */
    int    p_proto;      /* protocol number */
};
struct protoent *getprotobyname(const char *name);

extern char **environ;
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

/* from third_party/base64.h */
int base64_bufsize(int binsize, int options);
int base64_decode(const char *in_base64, int in_len, char *out_bin, int out_len);
int base64_encode(const char *in_bin, int in_len, char *out_base64, int out_len, int options);

/* from third_party/PMurHash.h */
void PMurHash32_Process(uint32_t *ph1, uint32_t *pcarry, const void *key, int len);
uint32_t PMurHash32_Result(uint32_t h1, uint32_t carry, uint32_t total_length);
uint32_t PMurHash32(uint32_t seed, const void *key, int len);

/* from <iconv.h> */
typedef struct iconv *iconv_t;

/* from src/lua/tnt_iconv.c */
iconv_t tnt_iconv_open(const char *tocode, const char *fromcode);
void    tnt_iconv_close(iconv_t cd);
size_t  tnt_iconv(iconv_t cd, const char **inbuf, size_t *inbytesleft,
                  char **outbuf, size_t *outbytesleft);

/* from openssl/engine.h */
typedef void ENGINE;

/* from openssl/err.h */
unsigned long ERR_get_error(void);
char *ERR_error_string(unsigned long e, char *buf);

/* from openssl/evp.h */
typedef struct {} EVP_MD_CTX;
typedef struct {} EVP_MD;
int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
const EVP_MD *EVP_get_digestbyname(const char *name);

/* from openssl/hmac.h */
typedef struct {} HMAC_CTX;
int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len,
                 const EVP_MD *md, ENGINE *impl);
int HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len);
int HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len);

/* from src/lib/crypto/crypto.c */
EVP_MD_CTX *crypto_EVP_MD_CTX_new(void);
void crypto_EVP_MD_CTX_free(EVP_MD_CTX *ctx);
HMAC_CTX *crypto_HMAC_CTX_new(void);
void crypto_HMAC_CTX_free(HMAC_CTX *ctx);

/* from src/lib/uuid/tt_uuid.h (inline functions) */
struct tt_uuid;
int tt_uuid_from_string(const char *in, struct tt_uuid *uu);
void tt_uuid_to_string(const struct tt_uuid *uu, char *out);
void tt_uuid_bswap(struct tt_uuid *uu);
bool tt_uuid_is_nil(const struct tt_uuid *uu);
bool tt_uuid_is_equal(const struct tt_uuid *lhs, const struct tt_uuid *rhs);

/* from src/lua/init.h (declaration of tarantool_lua_slab_cache) */
struct slab_cache;

/* from src/lua/buffer.c */
void *lua_static_aligned_alloc(size_t size, size_t alignment);

/* from src/lua/buffer.lua */

/**
 * Register is a buffer to use with FFI functions, which usually
 * operate with pointers to scalar values like int, char, size_t,
 * void *. To avoid doing 'ffi.new(<type>[1])' on each such FFI
 * function invocation, a module can use one of attributes of the
 * register union.
 *
 * Naming policy of the attributes is easy to remember:
 * 'a' for array type + type name first letters + 'p' for pointer.
 *
 * For example:
 * - int[1] - <a>rray of <i>nt - ai;
 * - const unsigned char *[1] -
 *       <a>rray of <c>onst <u>nsigned <c>har <p> pointer - acucp.
 */
union c_register {
    size_t as[1];
    void *ap[1];
    int ai[1];
    char ac[1];
    const unsigned char *acucp[1];
    unsigned long aul[1];
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int64_t i64;
};
