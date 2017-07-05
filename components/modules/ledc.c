// Module for working with the ledc driver

#include "module.h"
#include "lauxlib.h"
#include "platform.h"

#include "driver/ledc.h"

static int lledc_config( lua_State *L )
{
  int t=1;
  luaL_checkanytable (L, t);

  /* Setup timer */
  ledc_timer_config_t ledc_timer;

  lua_getfield(L, t, "bits");
  ledc_timer.bit_num = luaL_optint (L, -1, LEDC_TIMER_13_BIT);
  if(ledc_timer.bit_num < LEDC_TIMER_10_BIT || ledc_timer.bit_num > LEDC_TIMER_15_BIT)
    return luaL_error (L, "bits field out of range");

  lua_getfield(L, t, "frequency");
  ledc_timer.freq_hz = luaL_checkinteger(L, -1);

  lua_getfield(L, t, "mode");
  ledc_timer.speed_mode = luaL_checkinteger(L, -1);
  if(ledc_timer.speed_mode != LEDC_HIGH_SPEED_MODE && ledc_timer.speed_mode != LEDC_LOW_SPEED_MODE)
    return luaL_error (L, "Invalid mode");

  lua_getfield(L, t, "timer");
  ledc_timer.timer_num = luaL_checkinteger(L, -1);
  if(ledc_timer.timer_num < LEDC_TIMER_0 || ledc_timer.timer_num > LEDC_TIMER_3)
    return luaL_error (L, "Invalid timer");

  esp_err_t timerErr = ledc_timer_config(&ledc_timer);
  if(timerErr != ESP_OK)
    return luaL_error (L, "timer configuration failed code %d", timerErr);

  /* Setup channel */
  ledc_channel_config_t channel_config = {
    .speed_mode = ledc_timer.speed_mode,
    .timer_sel = ledc_timer.timer_num
  };

  lua_getfield(L, t, "channel");
  channel_config.channel = luaL_checkinteger(L, -1);
  if(channel_config.channel < LEDC_CHANNEL_0 || channel_config.channel > LEDC_CHANNEL_7)
    return luaL_error (L, "Invalid timer");

  lua_getfield(L, t, "duty");
  channel_config.duty = luaL_checkinteger(L, -1);

  lua_getfield(L, t, "gpio");
  channel_config.gpio_num = luaL_checkinteger(L, -1);
  if(!GPIO_IS_VALID_GPIO(channel_config.gpio_num))
    return luaL_error (L, "Invalid gpio");

  lua_getfield(L, t, "interupt");
  channel_config.intr_type = luaL_optint (L, -1, 0) > 0 ? LEDC_INTR_FADE_END : LEDC_INTR_DISABLE;

  esp_err_t channelErr = ledc_channel_config(&channel_config);
  if(channelErr != ESP_OK)
    return luaL_error (L, "channel configuration failed code %d", channelErr);

  return 1;
}

static void lledc_check_speed_argument( lua_State *L, int stack, int mode ) {
  luaL_argcheck(L, mode == LEDC_HIGH_SPEED_MODE || mode == LEDC_LOW_SPEED_MODE, stack, "Invalid mode");
}

static void lledc_check_channel_argument( lua_State *L, int stack, int channel ) {
  luaL_argcheck(L, channel >= LEDC_CHANNEL_0 && channel <= LEDC_CHANNEL_7, stack, "Invalid channel");
}

static void lledc_check_timer_argument( lua_State *L, int stack, int timer ) {
  luaL_argcheck(L, timer >= LEDC_TIMER_0 && timer <= LEDC_TIMER_3, stack, "Invalid channel");
}

static int lledc_stop( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int idleLevel = luaL_checkint (L, ++stack);
  luaL_argcheck(L, idleLevel >= 0 && idleLevel <= 1, stack, "Invalid idle level");

  esp_err_t err = ledc_stop(mode, channel, idleLevel);
  if(err != ESP_OK)
    return luaL_error (L, "stop failed, code %d", err);

  return 1;
}

static int lledc_set_freq( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int timer = luaL_checkint (L, ++stack);
  lledc_check_timer_argument(L, stack, timer);

  int frequency = luaL_checkint (L, ++stack);

  esp_err_t err = ledc_set_freq(mode, timer, frequency);
  if(err != ESP_OK)
    return luaL_error (L, "set freq failed, code %d", err);

  return 1;
}

static int lledc_get_freq( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int timer = luaL_checkint (L, ++stack);
  lledc_check_timer_argument(L, stack, timer);

  int frequency = ledc_get_freq(mode, timer);
  lua_pushinteger (L, frequency);

  return 1;
}

static int lledc_set_duty( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int duty = luaL_checkint (L, 3);

  esp_err_t dutyErr = ledc_set_duty(mode, channel, duty);
  if(dutyErr != ESP_OK)
    return luaL_error (L, "set duty failed, code %d", dutyErr);

  esp_err_t updateErr = ledc_update_duty(mode, channel);
  if(updateErr != ESP_OK)
    return luaL_error (L, "update duty failed, code %d", updateErr);

  return 1;
}

static int lledc_get_duty( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int duty = ledc_get_duty(mode, channel);
  lua_pushinteger (L, duty);

  return 1;
}

static int lledc_timer_rst( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int timer = luaL_checkint (L, ++stack);
  lledc_check_timer_argument(L, stack, timer);

  esp_err_t err = ledc_timer_rst(mode, timer);
  if(err != ESP_OK)
    return luaL_error (L, "reset failed, code %d", err);

  return 1;
}

static int lledc_timer_pause( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int timer = luaL_checkint (L, ++stack);
  lledc_check_timer_argument(L, stack, timer);

  esp_err_t err = ledc_timer_pause(mode, timer);
  if(err != ESP_OK)
    return luaL_error (L, "pause failed, code %d", err);

  return 1;
}

static int lledc_timer_resume( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int timer = luaL_checkint (L, ++stack);
  lledc_check_timer_argument(L, stack, timer);

  esp_err_t err = ledc_timer_resume(mode, timer);
  if(err != ESP_OK)
    return luaL_error (L, "resume failed, code %d", err);

  return 1;
}

static int lledc_set_fade_with_time( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int targetDuty = luaL_checkint (L, ++stack);
  int maxFadeTime = luaL_checkint (L, ++stack);

  int wait = luaL_optint (L, ++stack, LEDC_FADE_NO_WAIT);
  luaL_argcheck(L, wait == LEDC_FADE_NO_WAIT || wait == LEDC_FADE_WAIT_DONE, stack, "Invalid wait");

  ledc_fade_func_install(0);

  esp_err_t fadeErr = ledc_set_fade_with_time(mode, channel, targetDuty, maxFadeTime);
  if(fadeErr != ESP_OK)
    return luaL_error (L, "set fade failed, code %d", fadeErr);

  esp_err_t startErr = ledc_fade_start(mode, channel, wait);
  if(startErr != ESP_OK)
    return luaL_error (L, "start fade failed, code %d", startErr);

  return 1;
}

static int lledc_set_fade_with_step( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int targetDuty = luaL_checkint (L, ++stack);
  int scale = luaL_checkint (L, ++stack);
  int cycleNum = luaL_checkint (L, ++stack);

  int wait = luaL_optint (L, ++stack, LEDC_FADE_NO_WAIT);
  luaL_argcheck(L, wait == LEDC_FADE_NO_WAIT || wait == LEDC_FADE_WAIT_DONE, stack, "Invalid wait");

  ledc_fade_func_install(0);

  esp_err_t fadeErr = ledc_set_fade_with_step(mode, channel, targetDuty, scale, cycleNum);
  if(fadeErr != ESP_OK)
    return luaL_error (L, "set fade failed, code %d", fadeErr);

  esp_err_t startErr = ledc_fade_start(mode, channel, wait);
  if(startErr != ESP_OK)
    return luaL_error (L, "start fade failed, code %d", startErr);

  return 1;
}

static int lledc_set_fade( lua_State *L ) {
  int stack = 0;

  int mode = luaL_checkint (L, ++stack);
  lledc_check_speed_argument(L, stack, mode);

  int channel = luaL_checkint (L, ++stack);
  lledc_check_channel_argument(L, stack, channel);

  int duty = luaL_checkint (L, ++stack);
  int direction = luaL_checkint (L, ++stack);
  luaL_argcheck(L, direction == LEDC_DUTY_DIR_DECREASE || direction == LEDC_DUTY_DIR_INCREASE, stack, "Invalid direction");

  int scale = luaL_checkint (L, ++stack);
  int cycleNum = luaL_checkint (L, ++stack);
  int stepNum = luaL_checkint (L, ++stack);;

  int wait = luaL_optint (L, ++stack, LEDC_FADE_NO_WAIT);
  luaL_argcheck(L, wait == LEDC_FADE_NO_WAIT || wait == LEDC_FADE_WAIT_DONE, stack, "Invalid wait");

  ledc_fade_func_install(0);

  esp_err_t fadeErr = ledc_set_fade(mode, channel, duty, direction, stepNum, cycleNum, scale);
  if(fadeErr != ESP_OK)
    return luaL_error (L, "set fade failed, code %d", fadeErr);

  esp_err_t startErr = ledc_fade_start(mode, channel, wait);
  if(startErr != ESP_OK)
    return luaL_error (L, "start fade failed, code %d", startErr);

  return 1;
}

// Module function map
static const LUA_REG_TYPE ledc_map[] =
{
  { LSTRKEY( "config" ),           LFUNCVAL( lledc_config ) },

  { LSTRKEY( "getduty" ),         LFUNCVAL( lledc_get_duty ) },
  { LSTRKEY( "setduty" ),         LFUNCVAL( lledc_set_duty ) },
  { LSTRKEY( "getfreq" ),         LFUNCVAL( lledc_get_freq ) },
  { LSTRKEY( "setfreq" ),         LFUNCVAL( lledc_set_freq ) },

  { LSTRKEY( "stop" ),            LFUNCVAL( lledc_stop ) },
  { LSTRKEY( "reset" ),           LFUNCVAL( lledc_timer_rst ) },
  { LSTRKEY( "pause" ),           LFUNCVAL( lledc_timer_pause ) },
  { LSTRKEY( "resume" ),          LFUNCVAL( lledc_timer_resume ) },

  { LSTRKEY( "fadewithtime" ),    LFUNCVAL( lledc_set_fade_with_time ) },
  { LSTRKEY( "fadewithstep" ),    LFUNCVAL( lledc_set_fade_with_step ) },
  { LSTRKEY( "fade" ),            LFUNCVAL( lledc_set_fade ) },

  { LSTRKEY( "HIGH_SPEED"),       LNUMVAL( LEDC_HIGH_SPEED_MODE ) },
  { LSTRKEY( "LOW_SPEED"),        LNUMVAL( LEDC_LOW_SPEED_MODE ) },

  { LSTRKEY( "TIMER_0"),          LNUMVAL( LEDC_TIMER_0 ) },
  { LSTRKEY( "TIMER_1"),          LNUMVAL( LEDC_TIMER_1 ) },
  { LSTRKEY( "TIMER_2"),          LNUMVAL( LEDC_TIMER_2 ) },
  { LSTRKEY( "TIMER_10_BIT"),     LNUMVAL( LEDC_TIMER_10_BIT ) },
  { LSTRKEY( "TIMER_11_BIT"),     LNUMVAL( LEDC_TIMER_11_BIT ) },
  { LSTRKEY( "TIMER_12_BIT"),     LNUMVAL( LEDC_TIMER_12_BIT ) },
  { LSTRKEY( "TIMER_13_BIT"),     LNUMVAL( LEDC_TIMER_13_BIT ) },
  { LSTRKEY( "TIMER_14_BIT"),     LNUMVAL( LEDC_TIMER_14_BIT ) },
  { LSTRKEY( "TIMER_15_BIT"),     LNUMVAL( LEDC_TIMER_15_BIT ) },

  { LSTRKEY( "CHANNEL_0"),        LNUMVAL( LEDC_CHANNEL_0 ) },
  { LSTRKEY( "CHANNEL_1"),        LNUMVAL( LEDC_CHANNEL_1 ) },
  { LSTRKEY( "CHANNEL_2"),        LNUMVAL( LEDC_CHANNEL_2 ) },
  { LSTRKEY( "CHANNEL_3"),        LNUMVAL( LEDC_CHANNEL_3 ) },
  { LSTRKEY( "CHANNEL_4"),        LNUMVAL( LEDC_CHANNEL_4 ) },
  { LSTRKEY( "CHANNEL_5"),        LNUMVAL( LEDC_CHANNEL_5 ) },
  { LSTRKEY( "CHANNEL_6"),        LNUMVAL( LEDC_CHANNEL_6 ) },
  { LSTRKEY( "CHANNEL_7"),        LNUMVAL( LEDC_CHANNEL_7 ) },

  { LSTRKEY( "IDLE_LOW"),         LNUMVAL( 0 ) },
  { LSTRKEY( "IDLE_HIGH"),        LNUMVAL( 1 ) },

  { LSTRKEY( "FADE_NO_WAIT"),     LNUMVAL( LEDC_FADE_NO_WAIT ) },
  { LSTRKEY( "FADE_WAIT_DONE"),   LNUMVAL( LEDC_FADE_WAIT_DONE ) },
  { LSTRKEY( "FADE_DECREASE"),    LNUMVAL( LEDC_DUTY_DIR_DECREASE ) },
  { LSTRKEY( "FADE_INCREASE"),    LNUMVAL( LEDC_DUTY_DIR_INCREASE ) }
};

NODEMCU_MODULE(LEDC, "ledc", ledc_map, NULL);
