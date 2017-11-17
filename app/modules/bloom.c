/*
 * Module for bloom filters
 *
 * Philip Gladstone, N1DQ
 */

#include "module.h"
#include "lauxlib.h"
#include "c_types.h"
#include "../crypto/sha2.h"

#if defined(LUA_USE_MODULES_BLOOM) && !defined(SHA2_ENABLE)
#error Must have SHA2)ENABLE set for BLOOM module
#endif

typedef struct {
  uint8 fns;
  uint16 size;
  uint32 buf[];
} bloom_t;

static bool add_or_check(const uint8 *buf, size_t len, bloom_t *filter, bool add) {
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, buf, len);

  char hash[32];
  SHA256_Final(hash, &ctx);

  int i;
  uint32 bits = filter->size << 5;
  uint8 *h = hash;
  bool prev = true;
  for (i = 0; i < filter->fns; i++) {
    uint32 val = (((h[0] << 8) + h[1]) << 8) + h[2];
    h += 3;
    val = val % bits;

    uint32 offset = val >> 5;
    uint32 bit = 1 << (val & 31);

    if (!(filter->buf[offset] & bit)) {
      if (add) {
        filter->buf[offset] |= bit;
        prev = false;
      } else {
        return false;
      }
    }
  }

  return prev;
}

static int bloom_filter_check(lua_State *L) {
  bloom_t *filter = (bloom_t *)luaL_checkudata(L, 1, "bloom.filter");
  size_t length;
  const uint8 *buffer = (uint8 *) luaL_checklstring(L, 2, &length);

  bool rc = add_or_check(buffer, length, filter, false);

  lua_pushboolean(L, rc);
  return 1;
}

static int bloom_filter_add(lua_State *L) {
  bloom_t *filter = (bloom_t *)luaL_checkudata(L, 1, "bloom.filter");
  size_t length;
  const uint8 *buffer = (uint8 *) luaL_checklstring(L, 2, &length);

  bool rc = add_or_check(buffer, length, filter, true);

  lua_pushboolean(L, rc);
  return 1;
}

static int bloom_filter_info(lua_State *L) {
  bloom_t *filter = (bloom_t *)luaL_checkudata(L, 1, "bloom.filter");

  lua_pushinteger(L, filter->size << 5);
  lua_pushinteger(L, filter->fns);

  int total = 0;
  int i;
  for (i = 0; i < filter->size; i++) {
    uint32 v = filter->buf[i];

    while (v) {
      total++;
      v &= (v - 1);
    }
  }

  lua_pushinteger(L, total);

  return 3;
}

static int bloom_create(lua_State *L) {
  int items = luaL_checkinteger(L, 1);
  int error = luaL_checkinteger(L, 2);

  int n = error;
  int logp = 0;
  while (n > 0) {
    n = n >> 1;
    logp--;
  }

  int bits = -items * logp;
  bits += bits >> 1;

  bits = (bits + 31) & ~31;

  if (bits < 256) {
    bits = 256;
  }

  int size = bits >> 3;

  int fns = bits / items;
  fns = (fns >> 1) + fns / 6;

  if (fns < 2) {
    fns = 2;
  }
  if (fns > 10) {
    fns = 10;
  }

  bloom_t *filter = (bloom_t *) lua_newuserdata(L, sizeof(bloom_t) + size);

  memset(filter, 0, sizeof(bloom_t) + size);
  filter->size = size >> 2;
  filter->fns = fns;

  return 1;
}

static const LUA_REG_TYPE bloom_filter_map[] = {
  { LSTRKEY( "add" ),                   LFUNCVAL( bloom_filter_add ) },
  { LSTRKEY( "check" ),                 LFUNCVAL( bloom_filter_check ) },
  { LSTRKEY( "info" ),                  LFUNCVAL( bloom_filter_info ) },
  { LSTRKEY( "__index" ),               LROVAL( bloom_filter_map ) },
  { LNILKEY, LNILVAL }
};

// Module function map
static const LUA_REG_TYPE bloom_map[] = {
  { LSTRKEY( "create" ),   LFUNCVAL( bloom_create ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int bloom_open(lua_State *L) {
  luaL_rometatable(L, "bloom.filter", (void *)bloom_filter_map);  
  return 1;
}

NODEMCU_MODULE(BLOOM, "bloom", bloom_map, bloom_open);
