#ifndef PC_LANGUAGE_H
#define PC_LANGUAGE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the external language pack selected in settings.ini.
 * "en" always uses the original ROM resources. Other codes are loaded from
 * languages/<code>/aram and may override complete data/table pairs. */
void pc_language_init(const char* code);
void pc_language_shutdown(void);

/* ARAM integration used by jsyswrap.cpp. */
u32 pc_language_override_address(int resource_id, u32 original_address);
u32 pc_language_override_size(int resource_id, u32 original_size);
int pc_language_read_aram(u32 address, u8* dst, u32 size);

const char* pc_language_code(void);
int pc_language_is_external(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_LANGUAGE_H */
