#ifndef SDK_OVERRIDES_INCLUDE_USER_INTERFACE_H_
#define SDK_OVERRIDES_INCLUDE_USER_INTERFACE_H_

#include_next "user_interface.h"

bool wifi_softap_deauth(uint8 mac[6]);
uint8 get_fpm_auto_sleep_flag(void);

enum ext_flash_size_map {
    FLASH_SIZE_32M_MAP_2048_2048 = 7,
    FLASH_SIZE_64M_MAP = 8,
    FLASH_SIZE_128M_MAP = 9
};

// Documented in section 4.5 of 9b-esp8266_low_power_solutions_en.pdf
void system_deep_sleep_instant(uint32 time_in_us);

//force sleep API
#define FPM_SLEEP_MAX_TIME 268435455 //0xFFFFFFF
void wifi_fpm_set_wakeup_cb(void (*fpm_wakeup_cb_func)(void));
bool fpm_is_open(void);
bool fpm_rf_is_closed(void);
uint8 get_fpm_auto_sleep_flag(void);



#endif /* SDK_OVERRIDES_INCLUDE_USER_INTERFACE_H_ */
