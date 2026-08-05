/* pc_language.c - external, ROM-safe language packs for the PC port */
#include "pc_language.h"

#include "jsyswrap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PC_LANG_CODE_MAX 31
#define PC_LANG_PATH_MAX 512
#define PC_LANG_VIRTUAL_BASE 0xE0000000u
#define PC_LANG_MAX_FILE_SIZE (64u * 1024u * 1024u)
#define PC_LANG_MAX_TOTAL_SIZE (128u * 1024u * 1024u)

typedef struct {
    int resource_id;
    const char* filename;
    u8* data;
    u32 size;
    u32 padded_size;
    u32 virtual_address;
    int enabled;
} PCLanguageResource;

/* Only language-bearing resources are eligible. Models, game logic, audio and
 * save data can never be replaced by a language pack. */
static PCLanguageResource s_resources[] = {
    { RESOURCE_MAIL, "mail_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAIL_TABLE, "mail_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILA, "maila_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILA_TABLE, "maila_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILB, "mailb_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILB_TABLE, "mailb_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILC, "mailc_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MAILC_TABLE, "mailc_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_PS, "ps_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_PS_TABLE, "ps_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_PSZ, "psz_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_PSZ_TABLE, "psz_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SELECT, "select_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SELECT_TABLE, "select_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_STRING, "string_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_STRING_TABLE, "string_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SUPERZ, "superz_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SUPERZ_TABLE, "superz_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SUPER, "super_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_SUPER_TABLE, "super_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MESSAGE, "message_data.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_MESSAGE_TABLE, "message_data_table.bin", NULL, 0, 0, 0, 0 },
    { RESOURCE_NPC_NAME_STR_TABLE, "npc_name_str_table.bin", NULL, 0, 0, 0, 0 },
};

static const int s_pairs[][2] = {
    { RESOURCE_MAIL, RESOURCE_MAIL_TABLE },
    { RESOURCE_MAILA, RESOURCE_MAILA_TABLE },
    { RESOURCE_MAILB, RESOURCE_MAILB_TABLE },
    { RESOURCE_MAILC, RESOURCE_MAILC_TABLE },
    { RESOURCE_PS, RESOURCE_PS_TABLE },
    { RESOURCE_PSZ, RESOURCE_PSZ_TABLE },
    { RESOURCE_SELECT, RESOURCE_SELECT_TABLE },
    { RESOURCE_STRING, RESOURCE_STRING_TABLE },
    { RESOURCE_SUPERZ, RESOURCE_SUPERZ_TABLE },
    { RESOURCE_SUPER, RESOURCE_SUPER_TABLE },
    { RESOURCE_MESSAGE, RESOURCE_MESSAGE_TABLE },
};

static char s_code[PC_LANG_CODE_MAX + 1] = "en";
static int s_external = 0;

static u32 align32(u32 value) {
    return (value + 31u) & ~31u;
}

static int valid_code(const char* code) {
    size_t i;
    size_t len;
    if (!code) return 0;
    len = strlen(code);
    if (len == 0 || len > PC_LANG_CODE_MAX) return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)code[i];
        if (!(isalnum(c) || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static PCLanguageResource* find_resource(int resource_id) {
    size_t i;
    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        if (s_resources[i].resource_id == resource_id) return &s_resources[i];
    }
    return NULL;
}

static void clear_resources(void) {
    size_t i;
    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        free(s_resources[i].data);
        s_resources[i].data = NULL;
        s_resources[i].size = 0;
        s_resources[i].padded_size = 0;
        s_resources[i].virtual_address = 0;
        s_resources[i].enabled = 0;
    }
}

static int load_file(const char* path, u8** out_data, u32* out_size) {
    FILE* f;
    long length;
    u8* data;

    *out_data = NULL;
    *out_size = 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    length = ftell(f);
    if (length <= 0 || (unsigned long)length > PC_LANG_MAX_FILE_SIZE) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    data = (u8*)malloc((size_t)length);
    if (!data) { fclose(f); return 0; }
    if (fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out_data = data;
    *out_size = (u32)length;
    return 1;
}

static int table_is_sane(const u8* table, u32 table_size, u32 data_size) {
    u32 i;
    u32 previous = 0;
    if (!table || table_size == 0 || (table_size & 3u) != 0) return 0;
    for (i = 0; i < table_size; i += 4) {
        u32 end = ((u32)table[i] << 24) | ((u32)table[i + 1] << 16) |
                  ((u32)table[i + 2] << 8) | (u32)table[i + 3];
        if (end == 0) continue; /* unused/padded entries */
        if (end < previous || end > data_size) return 0;
        previous = end;
    }
    return previous > 0;
}

static void load_pair(const char* base, int data_id, int table_id, u32* total) {
    PCLanguageResource* data_res = find_resource(data_id);
    PCLanguageResource* table_res = find_resource(table_id);
    char data_path[PC_LANG_PATH_MAX];
    char table_path[PC_LANG_PATH_MAX];
    u8* data = NULL;
    u8* table = NULL;
    u32 data_size = 0;
    u32 table_size = 0;

    if (!data_res || !table_res) return;
    snprintf(data_path, sizeof(data_path), "%s/%s", base, data_res->filename);
    snprintf(table_path, sizeof(table_path), "%s/%s", base, table_res->filename);
    if (!load_file(data_path, &data, &data_size) ||
        !load_file(table_path, &table, &table_size)) {
        free(data);
        free(table);
        return;
    }
    if (!table_is_sane(table, table_size, data_size)) {
        printf("[Language] Ignoring invalid pair %s / %s\n", data_res->filename, table_res->filename);
        free(data);
        free(table);
        return;
    }
    if (*total + data_size + table_size > PC_LANG_MAX_TOTAL_SIZE) {
        printf("[Language] Pack exceeds memory safety limit\n");
        free(data);
        free(table);
        return;
    }
    data_res->data = data;
    data_res->size = data_size;
    data_res->padded_size = align32(data_size + 64u);
    data_res->enabled = 1;
    table_res->data = table;
    table_res->size = table_size;
    table_res->padded_size = align32(table_size + 64u);
    table_res->enabled = 1;
    *total += data_size + table_size;
}

static void load_single(const char* base, int resource_id, u32* total) {
    PCLanguageResource* res = find_resource(resource_id);
    char path[PC_LANG_PATH_MAX];
    u8* data = NULL;
    u32 size = 0;
    if (!res) return;
    snprintf(path, sizeof(path), "%s/%s", base, res->filename);
    if (!load_file(path, &data, &size)) return;
    if (*total + size > PC_LANG_MAX_TOTAL_SIZE) { free(data); return; }
    res->data = data;
    res->size = size;
    res->padded_size = align32(size + 64u);
    res->enabled = 1;
    *total += size;
}

static void assign_virtual_addresses(void) {
    size_t i;
    u32 cursor = PC_LANG_VIRTUAL_BASE;
    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        PCLanguageResource* res = &s_resources[i];
        if (!res->enabled) continue;
        res->virtual_address = cursor;
        cursor += res->padded_size + 32u;
        if (cursor < PC_LANG_VIRTUAL_BASE) {
            printf("[Language] Virtual address overflow; disabling %s\n", res->filename);
            res->enabled = 0;
        }
    }
}

void pc_language_init(const char* code) {
    char base[PC_LANG_PATH_MAX];
    size_t i;
    u32 total = 0;
    int enabled_count = 0;

    clear_resources();
    strcpy(s_code, "en");
    s_external = 0;

    if (!valid_code(code)) {
        printf("[Language] Invalid language code; using English ROM data\n");
        return;
    }
    strncpy(s_code, code, PC_LANG_CODE_MAX);
    s_code[PC_LANG_CODE_MAX] = '\0';
    if (strcmp(s_code, "en") == 0) {
        printf("[Language] English: using original ROM resources\n");
        return;
    }

    snprintf(base, sizeof(base), "languages/%s/aram", s_code);
    for (i = 0; i < sizeof(s_pairs) / sizeof(s_pairs[0]); i++) {
        load_pair(base, s_pairs[i][0], s_pairs[i][1], &total);
    }
    load_single(base, RESOURCE_NPC_NAME_STR_TABLE, &total);
    assign_virtual_addresses();

    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        if (s_resources[i].enabled) enabled_count++;
    }
    if (enabled_count == 0) {
        printf("[Language] No valid resources found for '%s'; using English ROM data\n", s_code);
        return;
    }
    s_external = 1;
    printf("[Language] Loaded '%s': %d resources, %u bytes; missing resources fall back to English\n",
           s_code, enabled_count, total);
}

void pc_language_shutdown(void) {
    clear_resources();
    s_external = 0;
    strcpy(s_code, "en");
}

u32 pc_language_override_address(int resource_id, u32 original_address) {
    PCLanguageResource* res = find_resource(resource_id);
    return (res && res->enabled) ? res->virtual_address : original_address;
}

u32 pc_language_override_size(int resource_id, u32 original_size) {
    PCLanguageResource* res = find_resource(resource_id);
    return (res && res->enabled) ? res->size : original_size;
}

int pc_language_read_aram(u32 address, u8* dst, u32 size) {
    size_t i;
    if (!dst || size == 0) return 0;
    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        PCLanguageResource* res = &s_resources[i];
        u32 offset;
        u32 available;
        u32 copy_size;
        if (!res->enabled || address < res->virtual_address) continue;
        offset = address - res->virtual_address;
        if (offset >= res->padded_size || size > res->padded_size - offset) continue;
        memset(dst, 0, size);
        if (offset < res->size) {
            available = res->size - offset;
            copy_size = size < available ? size : available;
            memcpy(dst, res->data + offset, copy_size);
        }
        return 1;
    }
    return 0;
}

const char* pc_language_code(void) {
    return s_code;
}

int pc_language_is_external(void) {
    return s_external;
}
