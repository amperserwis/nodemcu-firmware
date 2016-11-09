#ifndef __SW_TIMER_H__
#define __SW_TIMER_H__
#include "user_interface.h"
//#define SWTMR_DEBUG
#define USE_SWTMR_ERROR_STRINGS

#if defined(SWTMR_DEBUG) || defined(NODE_DEBUG)
  #ifndef SWTMR_DEBUG
    #define SWTMR_DEBUG
  #endif


#define SWTMR_DBG(fmt, ...) c_printf("\n SWTMR_DBG(%s):"fmt"\n", __FUNCTION__, ##__VA_ARGS__)
#else
  #define SWTMR_DBG(...)
#endif

#if defined(SWTMR_ERROR) || defined(NODE_ERROR)
  #define SWTMR_ERR(fmt, ...) c_printf("\n SWTMR:"fmt"\n", ##__VA_ARGS__)
#else
  #define SWTMR_DBG(...)
#endif

enum SWTMR_STATUS{
  SWTMR_OK = 0,

  SWTMR_MALLOC_FAIL = 10,
  SWTMR_TIMER_NOT_ARMED,
  SWTMR_NULL_PTR,

  SWTMR_REGISTRY_NO_REGISTERED_TIMERS,

  SWTMR_SUSPEND_ARRAY_INITIALIZATION_FAILED,
  SWTMR_SUSPEND_ARRAY_ADD_FAILED,
  SWTMR_SUSPEND_ARRAY_REMOVE_FAILED,
  SWTMR_SUSPEND_TIMER_ALREADY_SUSPENDED,
  SWTMR_SUSPEND_TIMER_ALREADY_REARMED,
  SWTMR_SUSPEND_NO_SUSPENDED_TIMERS,
  SWTMR_SUSPEND_TIMER_NOT_SUSPENDED,

};



/*      Global Function Declarations      */
void sw_timer_register(void* timer_ptr);
void sw_timer_unregister(void* timer_ptr);
int sw_timer_suspend(os_timer_t* timer_ptr);
int sw_timer_resume(os_timer_t* timer_ptr);
void swtmr_print_registry(void);
void swtmr_print_suspended(void);
void swtmr_print_timer_list(void);
const char* swtmr_errorcode2str(int error_value);
bool swtmr_suspended_test(os_timer_t* timer_ptr);
#endif // __SW_TIMER_H__
