#ifndef APP_H
#define APP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all system modules in correct order
 * @return true on success, false on failure
 */
bool app_init(void);

#ifdef __cplusplus
}
#endif

#endif  // APP_H
