#include "module.h"
#include "lauxlib.h"
#include "platform.h"
#include "c_stdlib.h"
#include "c_string.h"
#include "user_interface.h"
#include "driver/uart.h"

#define CANARY_VALUE 0x32383132

typedef struct {
  int canary;
  int size;
  uint8_t values[0];
} ws2812_buffer;

// Init UART1 to be able to stream WS2812 data
// We use GPIO2 as output pin
static void ICACHE_FLASH_ATTR ws2812_init() {
  // Configure UART1
  // Set baudrate of UART1 to 3200000
  WRITE_PERI_REG(UART_CLKDIV(1), UART_CLK_FREQ / 3200000);
  // Set UART Configuration No parity / 6 DataBits / 1 StopBits / Invert TX
  WRITE_PERI_REG(UART_CONF0(1), UART_TXD_INV | (1 << UART_STOP_BIT_NUM_S) | (1 << UART_BIT_NUM_S));

  // Pull GPIO2 down
  platform_gpio_mode(4, PLATFORM_GPIO_OUTPUT, PLATFORM_GPIO_FLOAT);
  platform_gpio_write(4, 0);

  // Waits 10us to simulate a reset
  os_delay_us(10);

  // Redirect UART1 to GPIO2
  // Disable GPIO2
  GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, BIT2);
  // Enable Function 2 for GPIO2 (U1TXD)
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
}


// Stream data using UART1 routed to GPIO2
// ws2812.init() should be called first
//
// NODE_DEBUG should not be activated because it also uses UART1
static void ICACHE_RAM_ATTR ws2812_write(uint8_t *pixels, uint32_t length) {

  // Data are sent LSB first, with a start bit at 0, an end bit at 1 and all inverted
  // 0b00110111 => 110111 => [0]111011[1] => 10001000 => 00
  // 0b00000111 => 000111 => [0]111000[1] => 10001110 => 01
  // 0b00110100 => 110100 => [0]001011[1] => 11101000 => 10
  // 0b00000100 => 000100 => [0]001000[1] => 11101110 => 11
  uint8_t _uartData[4] = { 0b00110111, 0b00000111, 0b00110100, 0b00000100 };

  uint8_t *end = pixels + length;

  do {
    uint8_t value = *pixels++;

    // Wait enough space in the FIFO buffer
    // (Less than 124 bytes in the buffer)
    while (((READ_PERI_REG(UART_STATUS(1)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT) > 124);

    // Fill the buffer
    WRITE_PERI_REG(UART_FIFO(1), _uartData[(value >> 6) & 3]);
    WRITE_PERI_REG(UART_FIFO(1), _uartData[(value >> 4) & 3]);
    WRITE_PERI_REG(UART_FIFO(1), _uartData[(value >> 2) & 3]);
    WRITE_PERI_REG(UART_FIFO(1), _uartData[(value >> 0) & 3]);

  } while(pixels < end);

}

// Lua: ws2812.write("string")
// Byte triples in the string are interpreted as G R B values.
//
// ws2812.init() should be called first
//
// ws2812.write(string.char(0, 255, 0)) sets the first LED red.
// ws2812.write(string.char(0, 0, 255):rep(10)) sets ten LEDs blue.
// ws2812.write(string.char(255, 0, 0, 255, 255, 255)) first LED green, second LED white.
static int ICACHE_FLASH_ATTR ws2812_writegrb(lua_State* L) {
  size_t length;
  const char *values;

  // Buffer or string
  if(lua_isuserdata(L, 1)) {
    ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 2);

    luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 2, "ws2812.buffer expected");

    values = &buffer->values[0];
    length = 3*buffer->size;
  } else {
    values = luaL_checklstring(L, 1, &length);
  }

  // Send the buffer
  ws2812_write((uint8_t*) values, length);

  return 0;
}

// Handle a buffer where we can store led values
static int ICACHE_FLASH_ATTR ws2812_new_buffer(lua_State *L) {
  const int leds = luaL_checkint(L, 1);

  luaL_argcheck(L, leds > 0, 1, "should be a positive integer");

  // Allocate memory
  size_t size = sizeof(ws2812_buffer) + 3*leds*sizeof(uint8_t);
  ws2812_buffer * buffer = (ws2812_buffer*)lua_newuserdata(L, size);

  // Associate its metatable
  luaL_getmetatable(L, "ws2812.buffer");
  lua_setmetatable(L, -2);

  // Save led strip size
  buffer->size = leds;

  // Store canary for future type checks
  buffer->canary = CANARY_VALUE;

  return 1;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_fill(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");

  const int g = luaL_checkinteger(L, 2);
  const int r = luaL_checkinteger(L, 3);
  const int b = luaL_checkinteger(L, 4);

  uint8_t * p = &buffer->values[0];
  int i;
  for(i = 0; i < buffer->size; i++) {
    *p++ = g;
    *p++ = r;
    *p++ = b;
  }

  return 0;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_fade(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);
  const int fade = luaL_checkinteger(L, 2);

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");
  luaL_argcheck(L, fade > 0, 2, "fade value should be a strict positive number");

  uint8_t * p = &buffer->values[0];
  int i;
  for(i = 0; i < buffer->size; i++) {
    *p++ /= fade;
    *p++ /= fade;
    *p++ /= fade;
  }

  return 0;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_get(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);
  const int led = luaL_checkinteger(L, 2) - 1;

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");
  luaL_argcheck(L, led >= 0 && led < buffer->size, 2, "index out of range");

  lua_pushnumber(L, buffer->values[3*led+0]);
  lua_pushnumber(L, buffer->values[3*led+1]);
  lua_pushnumber(L, buffer->values[3*led+2]);

  return 3;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_set(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);
  const int led = luaL_checkinteger(L, 2) - 1;

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");
  luaL_argcheck(L, led >= 0 && led < buffer->size, 2, "index out of range");

  int type = lua_type(L, 3);
  if(type == LUA_TTABLE)
  {
    // Get g,r,b values and push them on stash
    lua_rawgeti(L, 3, 1);
    lua_rawgeti(L, 3, 2);
    lua_rawgeti(L, 3, 3);

    // Convert them as int and store them in buffer
    buffer->values[3*led+0] = lua_tonumber(L, -3);
    buffer->values[3*led+1] = lua_tonumber(L, -2);
    buffer->values[3*led+2] = lua_tonumber(L, -1);

    // Clean up the stack
    lua_pop(L, 3);
  }
  else if(type == LUA_TSTRING)
  {
    size_t len;
    const char * buf = lua_tolstring(L, 3, &len);

    // Overflow check
    if( 3*led + len > 3*buffer->size )
    {
	return luaL_error(L, "string size will exceed strip length");
    }

    c_memcpy(&buffer->values[3*led], buf, len);
  }
  else
  {
    buffer->values[3*led+0] = luaL_checkinteger(L, 3);
    buffer->values[3*led+1] = luaL_checkinteger(L, 4);
    buffer->values[3*led+2] = luaL_checkinteger(L, 5);
  }

  return 0;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_size(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");

  lua_pushnumber(L, buffer->size);

  return 1;
}

static int ICACHE_FLASH_ATTR ws2812_buffer_write(lua_State* L) {
  ws2812_buffer * buffer = (ws2812_buffer*)lua_touserdata(L, 1);

  luaL_argcheck(L, buffer && buffer->canary == CANARY_VALUE, 1, "ws2812.buffer expected");

  // Send the buffer
  ws2812_write(buffer->values, 3*buffer->size);

  return 0;
}

static const LUA_REG_TYPE ws2812_buffer_map[] =
{
  { LSTRKEY( "fade" ),  LFUNCVAL( ws2812_buffer_fade )},
  { LSTRKEY( "fill" ),  LFUNCVAL( ws2812_buffer_fill )},
  { LSTRKEY( "get" ),   LFUNCVAL( ws2812_buffer_get )},
  { LSTRKEY( "set" ),   LFUNCVAL( ws2812_buffer_set )},
  { LSTRKEY( "size" ),  LFUNCVAL( ws2812_buffer_size )},
  { LSTRKEY( "write" ), LFUNCVAL( ws2812_buffer_write )},
  { LSTRKEY( "__index" ), LROVAL ( ws2812_buffer_map )},
  { LNILKEY, LNILVAL}
};

static const LUA_REG_TYPE ws2812_map[] =
{
  { LSTRKEY( "write" ), LFUNCVAL( ws2812_writegrb )},
  { LSTRKEY( "newBuffer" ), LFUNCVAL( ws2812_new_buffer )},
  { LSTRKEY( "init" ),     LFUNCVAL( ws2812_init )},
  { LNILKEY, LNILVAL}
};

int luaopen_ws2812(lua_State *L) {
  // TODO: Make sure that the GPIO system is initialized
  luaL_rometatable(L, "ws2812.buffer", (void *)ws2812_buffer_map);  // create metatable for ws2812.buffer
  return 0;
}

NODEMCU_MODULE(WS2812, "ws2812", ws2812_map, luaopen_ws2812);
