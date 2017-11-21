// Module for interfacing with WIFI

#include "module.h"
#include "lauxlib.h"
#include "lapi.h"
#include "platform.h"

#include "c_string.h"
#include "c_stdlib.h"
#include "ctype.h"

#include "c_types.h"
#include "user_interface.h"
#include "wifi_common.h"
#include "sys/network_80211.h"

static int recv_cb;
static uint8 mon_offset;
static uint8 mon_value;
static uint8 mon_mask;
static task_handle_t tasknumber;

static int8 variable_start[16] = {
   4,   // assoc req
   6,   // assoc response
  10,   // reassoc req
   6,   // reassoc resp
   0,   // probe req
  12,   // probe resp
  -1,
  -1,
  12,   // beacon
  -1,   // ATIM
   2,   // Disassociation
   6,   // authentication
   2,   // Deauthentication
   2,   // action
  -1,
  -1
};

typedef struct {
  uint16 len;
  uint8 buf[];
} packet_t;

static const LUA_REG_TYPE packet_function_map[];

static void wifi_rx_cb(uint8 *buf, uint16 len) {
  if (len != sizeof(struct sniffer_buf2)) {
    return;
  }

  struct sniffer_buf2 *snb = (struct sniffer_buf2 *) buf;
  management_request_t *mgt = (management_request_t *) snb->buf;

  if (mon_offset > len) {
    return;
  }

  if ((buf[mon_offset] & mon_mask) != mon_value) {
    return;
  }

  packet_t *packet = (packet_t *) c_malloc(len + sizeof(packet_t));
  if (packet) {
    packet->len = len;
    memcpy(packet->buf, buf, len);
    if (!task_post_medium(tasknumber, (ETSParam) packet)) {
      c_free(packet);
    }
  }
}

static void monitor_task(os_param_t param, uint8_t prio) 
{
  packet_t *input = (packet_t *) param;
  (void) prio;

  lua_State *L = lua_getstate();

  lua_rawgeti(L, LUA_REGISTRYINDEX, recv_cb);

  packet_t *packet = (packet_t *) lua_newuserdata(L, input->len + sizeof(packet_t));
  packet->len = input->len;
  memcpy(packet->buf, input->buf, input->len);
  luaL_getmetatable(L, "wifi.packet");
  lua_setmetatable(L, -2);

  c_free(input);

  lua_call(L, 1, 0);
}

static int packet_getraw(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");

  lua_pushlstring(L, (const char *) packet->buf, packet->len);
  return 1;
}

static ptrdiff_t posrelat (ptrdiff_t pos, size_t len) {
  /* relative string position: negative means back from end */
  if (pos < 0) pos += (ptrdiff_t)len + 1;
  return (pos >= 0) ? pos : 0;
}

static int packet_sub(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");

  ptrdiff_t start = posrelat(luaL_checkinteger(L, 2), packet->len);
  ptrdiff_t end = posrelat(luaL_optinteger(L, 3, -1), packet->len);

  if (start < 1) start = 1;
  if (end > (ptrdiff_t)packet->len) end = (ptrdiff_t)packet->len;
  if (start <= end) {
    lua_pushlstring(L, packet->buf+start-1, end-start+1);
  } else {
    lua_pushliteral(L, "");
  }

  return 1;
}

static void push_hex_string(lua_State *L, uint8 *buf, int len, char *sep) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);

  int i;
  for (i = 0; i < len; i++) {
    if (i && sep && *sep) {
      luaL_addstring(&b, sep);
    }
    char hex[3];

    uint8 c = buf[i];

    hex[0] = "0123456789abcdef"[c >> 4];
    hex[1] = "0123456789abcdef"[c & 0xf];
    hex[2] = 0;
    luaL_addstring(&b, hex);
  }
  
  luaL_pushresult(&b);
}

static void push_hex_string_colon(lua_State *L, uint8 *buf, int len) {
  push_hex_string(L, buf, len, ":");
}

static bool push_field_value_string(lua_State *L, management_request_t *mgt, 
    const uint8 *packet_end, const char *field) {

  if (strcmp(field, "subtype") == 0) {
    lua_pushinteger(L, mgt->framectrl.Subtype);
  } else if (strcmp(field, "dstmac") == 0) {
    push_hex_string_colon(L, mgt->rdaddr, sizeof(mgt->rdaddr));
  } else if (strcmp(field, "srcmac") == 0) {
    push_hex_string_colon(L, mgt->tsaddr, sizeof(mgt->rdaddr));
  } else if (strcmp(field, "bssid") == 0) {
    push_hex_string_colon(L, mgt->bssid, sizeof(mgt->rdaddr));
  } else if (strcmp(field, "header") == 0) {
    int varstart = variable_start[mgt->framectrl.Subtype];
    if (varstart > 0) {
      lua_pushlstring(L, (uint8 *) (mgt + 1), varstart);
    } else {
      return false;
    }
  } else {
    return false;
  }

  return true;
}

static bool push_field_value_int(lua_State *L, management_request_t *mgt, 
    const uint8 *packet_end, int field) {

  int varstart = variable_start[mgt->framectrl.Subtype];
  if (varstart >= 0) {
    uint8 *var = (uint8 *) (mgt + 1) + varstart;

    while (var + 2 <= packet_end && var + 2 + var[1] <= packet_end) {
      if (*var == field) {
        lua_pushlstring(L, var + 2, var[1]);
        return true;
      }
    }
  }

  return false;
}

static int packet_map_lookup(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");
  struct RxControl *rxc = (struct RxControl *) packet->buf; 
  management_request_t *mgt = (management_request_t *) (rxc + 1);
  const uint8 *packet_end = packet->buf + packet->len;

  if ((void *) (mgt + 1) > (void *) packet_end) {
    return 0;
  }
  if (mgt->framectrl.Type != FRAME_TYPE_MANAGEMENT) {
    return 0;
  }

  if (lua_type(L, 2) == LUA_TNUMBER) {
    int field = luaL_checkinteger(L, 2);
    if (push_field_value_int(L, mgt, packet_end, field)) {
      return 1;
    }
  } else {
    const char *field = luaL_checkstring(L, 2);
    if (push_field_value_string(L, mgt, packet_end, field)) {
      return 1;
    }

    // Now search the packet function map
    const TValue *res = luaR_findentry((void *) packet_function_map, field, 0, NULL);
    if (res) {
      luaA_pushobject(L, res);
      return 1;
    }
  }

  return 0;
}

static int packet_management(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");
  struct RxControl *rxc = (struct RxControl *) packet->buf; 
  management_request_t *mgt = (management_request_t *) (rxc + 1);
  const uint8 *packet_end = packet->buf + packet->len;

  if ((void *) (mgt + 1) > (void *) packet_end) {
    return 0;
  }
  if (mgt->framectrl.Type != FRAME_TYPE_MANAGEMENT) {
    return 0;
  }

  lua_newtable(L);

  lua_pushinteger(L, mgt->framectrl.Subtype);
  lua_setfield(L, -2, "subtype");

  push_hex_string_colon(L, mgt->rdaddr, sizeof(mgt->rdaddr));
  lua_setfield(L, -2, "dstmac");
  push_hex_string_colon(L, mgt->tsaddr, sizeof(mgt->tsaddr));
  lua_setfield(L, -2, "srcmac");
  push_hex_string_colon(L, mgt->bssid, sizeof(mgt->bssid));
  lua_setfield(L, -2, "bssid");

  int varstart = variable_start[mgt->framectrl.Subtype];
  if (varstart >= 0) {
    if (varstart > 0) {
      lua_pushlstring(L, (uint8 *) (mgt + 1), varstart);
      lua_setfield(L, -2, "header");
    }

    uint8 *var = (uint8 *) (mgt + 1) + varstart;

    while (var + 2 <= packet_end && var + 2 + var[1] <= packet_end) {
      lua_pushlstring(L, var + 2, var[1]);
      lua_rawseti(L, -2, *var);
      var += 2 + var[1];
    }
  }

  return 1;
}

static int packet_byte(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");

  int offset = luaL_checkinteger(L, 2);

  if (offset < 1 || offset > packet->len) {
    return 0;
  }

  lua_pushinteger(L, packet->buf[offset - 1]);

  return 1;
}

static int packet_subhex(lua_State *L) {
  packet_t *packet = luaL_checkudata(L, 1, "wifi.packet");

  ptrdiff_t start = posrelat(luaL_checkinteger(L, 2), packet->len);
  ptrdiff_t end = posrelat(luaL_optinteger(L, 3, -1), packet->len);
  const char *sep = luaL_optstring(L, 4, "");

  if (start < 1) start = 1;
  if (end > (ptrdiff_t)packet->len) end = (ptrdiff_t)packet->len;
  if (start <= end) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    int i;
    for (i = start - 1; i < end; i++) {
      char hex[3];

      if (i >= start) {
        luaL_addstring(&b, sep);
      }

      uint8 c = packet->buf[i];

      hex[0] = "0123456789abcdef"[c >> 4];
      hex[1] = "0123456789abcdef"[c & 0xf];
      hex[2] = 0;
      luaL_addstring(&b, hex);
    }
    
    luaL_pushresult(&b);
  } else {
    lua_pushliteral(L, "");
  }

  return 1;
}

static int wifi_monitor_start(lua_State *L) {
  int argno = 1;
  if (lua_type(L, argno) == LUA_TNUMBER) {
    int offset = luaL_checkinteger(L, argno);
    argno++;
    if (lua_type(L, argno) == LUA_TNUMBER) {
      int value = luaL_checkinteger(L, argno);

      int mask = 0xff;
      argno++;

      if (lua_type(L, argno) == LUA_TNUMBER) {
        mask = luaL_checkinteger(L, argno);
        argno++;
      }
      mon_offset = offset - 1;
      mon_value = value;
      mon_mask = mask;
    } else {
      return luaL_error(L, "Must supply offset and value");
    }
  } else {
    // Management frames by default
    mon_offset = 12;
    mon_value = 0x00;
    mon_mask = 0x0C;
  }
  if (lua_type(L, argno) == LUA_TFUNCTION || lua_type(L, argno) == LUA_TLIGHTFUNCTION)
  {
    lua_pushvalue(L, argno);  // copy argument (func) to the top of stack
    recv_cb = luaL_ref(L, LUA_REGISTRYINDEX);
    // this is very delicate code. If the timing is wrong, then the code crashes
    // as it appears that the sniffer buffers have not been allocated.
    wifi_station_set_auto_connect(0);
    os_delay_us(1000);
    wifi_set_opmode_current(1);
    os_delay_us(1000);
    wifi_promiscuous_enable(0);
    os_delay_us(1000);
    wifi_station_disconnect();
    os_delay_us(1000);
    wifi_set_promiscuous_rx_cb(wifi_rx_cb);
    wifi_set_channel(1);
    wifi_promiscuous_enable(1);
    return 0;
  }
  return luaL_error(L, "Missing callback");
}

static int wifi_monitor_channel(lua_State *L) {
  lua_pushinteger(L, wifi_get_channel());
  if (lua_type(L, 1) == LUA_TNUMBER) {
    int channel = luaL_checkinteger(L, 1);

    if (channel < 1 || channel > 15) {
      return luaL_error(L, "Channel number (%d) is out of range", channel);
    }
    wifi_set_channel(channel);
  }

  return 1;
}

static int wifi_monitor_stop(lua_State *L) {
  wifi_promiscuous_enable(0);
  luaL_unref(L, LUA_REGISTRYINDEX, recv_cb);
  recv_cb = LUA_NOREF;
  return 0;
}

static const LUA_REG_TYPE packet_function_map[] = {
  { LSTRKEY( "raw" ),         LFUNCVAL( packet_getraw ) },
  { LSTRKEY( "byte" ),        LFUNCVAL( packet_byte ) },
  { LSTRKEY( "sub" ),         LFUNCVAL( packet_sub ) },
  { LSTRKEY( "subhex" ),      LFUNCVAL( packet_subhex ) },
  { LSTRKEY( "management" ),  LFUNCVAL( packet_management ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE packet_map[] = {
  { LSTRKEY( "__index" ),     LFUNCVAL( packet_map_lookup ) },
  { LNILKEY, LNILVAL }
};

// Module function map
const LUA_REG_TYPE wifi_monitor_map[] = {
  { LSTRKEY( "start" ),      LFUNCVAL( wifi_monitor_start ) },
  { LSTRKEY( "stop" ),       LFUNCVAL( wifi_monitor_stop ) },
  { LSTRKEY( "channel" ),    LFUNCVAL( wifi_monitor_channel ) },
  { LNILKEY, LNILVAL }
};

int wifi_monitor_init(lua_State *L)
{
  luaL_rometatable(L, "wifi.packet", (void *)packet_map);
  tasknumber = task_get_id(monitor_task);
  return 0;
}

//NODEMCU_MODULE(WIFI_MONITOR, "wifi_monitor", wifi_monitor_map, wifi_monitor_init);
