#include <limits.h>
#include <string.h>
#include "lauxlib.h"
#include "lmem.h"
#include "mbedtls/md.h"
#include "module.h"
#include "platform.h"
#include "sodium_module.h"

#define HASH_METATABLE "crypto.hasher"

// Must not overlap with mbedtls_md_type_t
#define SODIUM_BLAKE2B (-1)

// algo_info_t describes a hashing algorithm and output size
typedef struct {
    const char* name;
    const size_t size;
    const int type; // either a mbedtls_md_type_t or SODIUM_BLAKE2B
} algo_info_t;

// hash_context_t contains information about an ongoing hash operation
typedef struct {
    mbedtls_md_context_t mbedtls_context;
    const algo_info_t* ainfo;
    bool hmac_mode;
} hash_context_t;

// the constant algorithms array below contains a table of functions and other
// information about each enabled hashing algorithm
static const algo_info_t algorithms[] = {
    { "MD5",       16, MBEDTLS_MD_MD5    },
    { "RIPEMD160", 20, MBEDTLS_MD_RIPEMD160 },
    { "SHA1",      20, MBEDTLS_MD_SHA1   },
    { "SHA224",    32, MBEDTLS_MD_SHA224 },
    { "SHA256",    32, MBEDTLS_MD_SHA256 },
    { "SHA384",    64, MBEDTLS_MD_SHA384 },
    { "SHA512",    64, MBEDTLS_MD_SHA512 },
#ifdef CONFIG_NODEMCU_CMODULE_SODIUM
    { "BLAKE2b",   64, SODIUM_BLAKE2B }, // The hash size isn't fixed with BLAKE2b, but 64 is the maximum
#endif
};


const int MAX_HASH_SIZE = 64; // Must be >= all the sizes in the above array

//NUM_ALGORITHMS contains the actual number of enabled algorithms
const int NUM_ALGORITHMS = sizeof(algorithms) / sizeof(algo_info_t);

static const algo_info_t* crypto_get_algo(lua_State* L, bool is_hmac)
{
    const algo_info_t *ainfo = NULL;
    const char *algo = luaL_checkstring(L, 1);

    for (int i = 0; i < NUM_ALGORITHMS; i++) {
        if (strcasecmp(algo, algorithms[i].name) == 0) {
            ainfo = &algorithms[i];
            break;
        }
    }

    if (ainfo == NULL) {
        luaL_error(L, "Unsupported algorithm: %s", algo);
    }

    if (ainfo->type == SODIUM_BLAKE2B && is_hmac) {
        luaL_error(L, "'BLAKE2b' algorithm does not support HMAC");
    } 

    return ainfo;
}

// crypto_new_hash (LUA: hasher = crypto.new_hash(algo)) allocates
// a hashing context for the requested algorithm
static int crypto_new_hash_or_hmac(lua_State* L, bool is_hmac) {
    const algo_info_t *ainfo = crypto_get_algo(L, is_hmac);
    const unsigned char *key = NULL;
    size_t key_len = 0;

#ifdef CONFIG_NODEMCU_CMODULE_SODIUM
    if (ainfo->type == SODIUM_BLAKE2B) {
        lua_remove(L, 1); // algo
        // Stack is now correct for calling l_sodium_generichash_init.
        // Even though the underlying crypto_generichash_init() doesn't
        // explicitly state it uses BLAKE2b (there's a separate
        // crypto_generichash_blake2b_init API for that) in practice it is
        // documented to do so, so I don't think it's necessary to worry about
        // the distinction here.
        return l_sodium_generichash_init(L);
    }
#endif

    if (is_hmac) {
        key = (const unsigned char *)luaL_checklstring(L, 2, &key_len);
    }

    // Instantiate a hasher object as a Lua userdata object
    // it will contain a pointer to a hash_context_t structure in which
    // we will store the mbedtls context information and also
    // what hashing algorithm this context is for.
    hash_context_t* phctx = (hash_context_t*)lua_newuserdata(L, sizeof(hash_context_t));
    luaL_getmetatable(L, HASH_METATABLE);
    lua_setmetatable(L, -2);

    phctx->ainfo = ainfo;
    phctx->hmac_mode = is_hmac;

    mbedtls_md_init(&phctx->mbedtls_context);
    int err =
        mbedtls_md_setup(
            &phctx->mbedtls_context,
            mbedtls_md_info_from_type(phctx->ainfo->type),
            is_hmac);
    if (phctx->hmac_mode)
        err |= mbedtls_md_hmac_starts(&phctx->mbedtls_context, key, key_len);
    else
        err |= mbedtls_md_starts(&phctx->mbedtls_context);
    if (err != 0)
        return luaL_error(L, "Error starting context");

    return 1;  // one object returned, the hasher userdata object.
}

static int crypto_new_hash(lua_State* L) {
  return crypto_new_hash_or_hmac(L, false);
}

static int crypto_new_hmac(lua_State* L)
{
  return crypto_new_hash_or_hmac(L, true);
}


// crypto_hash_update (LUA: hasher:update(data)) submits data
// to be hashed.
static int crypto_hash_update(lua_State* L) {
    // retrieve the hashing context:
    hash_context_t* phctx = (hash_context_t*)luaL_checkudata(L, 1, HASH_METATABLE);

    size_t size;  // size of the input string
    // retrieve the input string:
    const unsigned char* input = (const unsigned char*)luaL_checklstring(L, 2, &size);

    int err = 0;
    // call the update hashing function:
    if (phctx->hmac_mode)
        err = mbedtls_md_hmac_update(&phctx->mbedtls_context, input, size);
    else
        err = mbedtls_md_update(&phctx->mbedtls_context, input, size);

    if (err != 0)
        luaL_error(L, "Error updating hash");

    return 0;  // no return value
}

// crypto_hash_finalize (LUA: hasher:finalize()) returns the hash result
// as a binary string.
static int crypto_hash_finalize(lua_State* L) {
    // retrieve the hashing context:
    hash_context_t* phctx = (hash_context_t*)luaL_checkudata(L, 1, HASH_METATABLE);

    // reserve some space to retrieve the output hash
    unsigned char output[MAX_HASH_SIZE];

    int err = 0;
    // call the hash finish function to retrieve the result
    if (phctx->hmac_mode)
      err = mbedtls_md_hmac_finish(&phctx->mbedtls_context, output);
    else
      err = mbedtls_md_finish(&phctx->mbedtls_context, output);
    if (err != 0)
        luaL_error(L, "Error finalizing hash");

    // pack the output into a lua string
    lua_pushlstring(L, (const char*)output, phctx->ainfo->size);

    return 1;  // 1 result returned, the hash.
}

// crypto_hash_gc is called automatically by LUA when the hasher object is
// dereferenced, in order to free resources associated with the hashing process.
static int crypto_hash_gc(lua_State* L) {
    // retrieve the hashing context:
    hash_context_t* phctx = (hash_context_t*)luaL_checkudata(L, 1, HASH_METATABLE);
    mbedtls_md_free(&phctx->mbedtls_context);
    return 0;
}

static int crypto_hash(lua_State* L) {
    const algo_info_t *ainfo = crypto_get_algo(L, false);
#ifdef CONFIG_NODEMCU_CMODULE_SODIUM
    if (ainfo->type == SODIUM_BLAKE2B) {
        lua_remove(L, 1); // algo
        // Stack is now correct for calling l_sodium_generichash().
        return l_sodium_generichash(L);
    }
#endif
    size_t size;
    const unsigned char* input = (const unsigned char*)luaL_checklstring(L, 2, &size);
    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type(ainfo->type);
    unsigned char output[MAX_HASH_SIZE];
    int err = mbedtls_md(mdinfo, input, size, output);
    if (err != 0) {
        return luaL_error(L, "Error calculating hash");
    }
    lua_pushlstring(L, (const char*)output, ainfo->size);
    return 1;
}

static int crypto_hmac(lua_State* L) {
    const algo_info_t *ainfo = crypto_get_algo(L, true);
    size_t key_len = 0;
    const unsigned char *key = (const unsigned char *)luaL_checklstring(L, 2, &key_len);

    size_t size;
    const unsigned char* input = (const unsigned char*)luaL_checklstring(L, 3, &size);

    const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type(ainfo->type);
    unsigned char output[MAX_HASH_SIZE];
    int err = mbedtls_md_hmac(mdinfo, key, key_len, input, size, output);
    if (err != 0) {
        return luaL_error(L, "Error calculating HMAC");
    }
    lua_pushlstring(L, (const char*)output, ainfo->size);
    return 1;
}

// The following table defines methods of the hasher object
LROT_BEGIN(crypto_hasher)
    LROT_FUNCENTRY(update,   crypto_hash_update)
    LROT_FUNCENTRY(finalize, crypto_hash_finalize)
    LROT_FUNCENTRY(__gc,     crypto_hash_gc)
    LROT_TABENTRY(__index,   crypto_hasher)
LROT_END(crypto_hasher, NULL, 0)

// This table defines the functions of the crypto module:
LROT_BEGIN(crypto)
    LROT_FUNCENTRY(hash, crypto_hash)
    LROT_FUNCENTRY(hmac, crypto_hmac)
    LROT_FUNCENTRY(new_hash, crypto_new_hash)
    LROT_FUNCENTRY(new_hmac, crypto_new_hmac)
LROT_END(crypto, NULL, 0)

// luaopen_crypto is the crypto module initialization function
int luaopen_crypto(lua_State* L) {
    luaL_rometatable(L, HASH_METATABLE, (void*)crypto_hasher_map);  // create metatable for crypto.hash

    return 0;
}

// define the crypto NodeMCU module
NODEMCU_MODULE(CRYPTO, "crypto", crypto, luaopen_crypto);
