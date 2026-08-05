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

/* PAL Multi5 grammar bridge. Grammar values are stored in otherwise-unused
 * article fields only on PC and are never written to save data. */
#define PC_LANGUAGE_GRAMMAR_NONE (-1)
int pc_language_strip_grammar_prefix(const u8** str, int* len);
int pc_language_grammar_for_name(const u8* str, int len);
int pc_language_grammar_is_packed(int value);

/* Replaces one custom AGB_DUMMY0 grammar control with the selected text.
 * The returned value is the new buffer length. */
int pc_language_expand_custom_control(u8* data, int idx, int len,
                                      int free_grammar, int item_grammar,
                                      int player_is_female);

#ifdef __cplusplus
}
#endif

#endif /* PC_LANGUAGE_H */
