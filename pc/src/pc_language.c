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

#define PC_LANG_GRAMMAR_MAGIC 0x4C000000
#define PC_LANG_GRAMMAR_MASK  0xFF000000
#define PC_LANG_ITEM_NAME_LEN 16u
#define PC_LANG_CUSTOM_CMD 100u

/* Runtime asset arrays are writable on TARGET_PC and are initialized from the
 * user's USA disc before pc_language_init(). A PAL pack may safely replace
 * only these text arrays after that initialization. */
extern u8 itemName_paper[];
extern u8 itemName_money[];
extern u8 itemName_tool[];
extern u8 itemName_fish[];
extern u8 itemName_cloth[];
extern u8 itemName_etc[];
extern u8 itemName_carpet[];
extern u8 itemName_wall[];
extern u8 itemName_fruit[];
extern u8 itemName_plant[];
extern u8 itemName_minidisk[];
extern u8 itemName_dummy[];
extern u8 itemName_ticket[];
extern u8 itemName_insect[];
extern u8 itemName_hukubukuro[];
extern u8 itemName_kabu[];
extern u8 ftrName_table[];
extern u8 ftrName2_table[];

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


typedef struct {
    const char* filename;
    u8* data;
    u32 size;
} PCLanguageBlob;

typedef struct {
    const char* name_filename;
    u8* target;
    u32 target_size;
    const char* grammar_filename;
    u32 grammar_offset;
} PCLanguageNameBlock;

static PCLanguageBlob s_grammar_blobs[] = {
    { "artInfo_Paper.bin", NULL, 0 }, { "artInfo_Tool.bin", NULL, 0 },
    { "artInfo_Fish.bin", NULL, 0 }, { "artInfo_Cloth.bin", NULL, 0 },
    { "artInfo_Carpet.bin", NULL, 0 }, { "artInfo_Wall.bin", NULL, 0 },
    { "artInfo_Fruit.bin", NULL, 0 }, { "artInfo_Plant.bin", NULL, 0 },
    { "artInfo_MiniDisk.bin", NULL, 0 }, { "artInfo_Diary.bin", NULL, 0 },
    { "artInfo_Insect.bin", NULL, 0 }, { "artInfo_Money.bin", NULL, 0 },
    { "artInfo_Etc.bin", NULL, 0 }, { "artInfo_Ticket.bin", NULL, 0 },
    { "artInfo_Hukubukuro.bin", NULL, 0 }, { "artInfo_Kabu.bin", NULL, 0 },
    { "ftrArt.bin", NULL, 0 },
};

static PCLanguageNameBlock s_name_blocks[] = {
    { "itemName_paper.bin", itemName_paper, 0x1000, "artInfo_Paper.bin", 0 },
    { "itemName_money.bin", itemName_money, 0x40, "artInfo_Money.bin", 0 },
    { "itemName_tool.bin", itemName_tool, 0x5C0, "artInfo_Tool.bin", 0 },
    { "itemName_fish.bin", itemName_fish, 0x280, "artInfo_Fish.bin", 0 },
    { "itemName_cloth.bin", itemName_cloth, 0xFF0, "artInfo_Cloth.bin", 0 },
    { "itemName_etc.bin", itemName_etc, 0x310, "artInfo_Etc.bin", 0 },
    { "itemName_carpet.bin", itemName_carpet, 0x430, "artInfo_Carpet.bin", 0 },
    { "itemName_wall.bin", itemName_wall, 0x430, "artInfo_Wall.bin", 0 },
    { "itemName_fruit.bin", itemName_fruit, 0x80, "artInfo_Fruit.bin", 0 },
    { "itemName_plant.bin", itemName_plant, 0xB0, "artInfo_Plant.bin", 0 },
    { "itemName_minidisk.bin", itemName_minidisk, 0x370, "artInfo_MiniDisk.bin", 0 },
    { "itemName_dummy.bin", itemName_dummy, 0x100, NULL, 0 },
    { "itemName_ticket.bin", itemName_ticket, 0x600, "artInfo_Ticket.bin", 0 },
    { "itemName_insect.bin", itemName_insect, 0x2D0, "artInfo_Insect.bin", 0 },
    { "itemName_hukubukuro.bin", itemName_hukubukuro, 0x20, "artInfo_Hukubukuro.bin", 0 },
    { "itemName_kabu.bin", itemName_kabu, 0x40, "artInfo_Kabu.bin", 0 },
    { "ftrName_table.bin", ftrName_table, 0x4000, "ftrArt.bin", 0 },
    { "ftrName2_table.bin", ftrName2_table, 0xF20, "ftrArt.bin", 0xC00 },
};

static int s_item_file_count = 0;
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


static PCLanguageBlob* find_grammar_blob(const char* filename) {
    size_t i;
    if (!filename) return NULL;
    for (i = 0; i < sizeof(s_grammar_blobs) / sizeof(s_grammar_blobs[0]); i++) {
        if (strcmp(s_grammar_blobs[i].filename, filename) == 0) return &s_grammar_blobs[i];
    }
    return NULL;
}

static void clear_item_language(void) {
    size_t i;
    for (i = 0; i < sizeof(s_grammar_blobs) / sizeof(s_grammar_blobs[0]); i++) {
        free(s_grammar_blobs[i].data);
        s_grammar_blobs[i].data = NULL;
        s_grammar_blobs[i].size = 0;
    }
    s_item_file_count = 0;
}

static void load_item_language(const char* base, u32* total) {
    size_t i;
    char path[PC_LANG_PATH_MAX];
    for (i = 0; i < sizeof(s_grammar_blobs) / sizeof(s_grammar_blobs[0]); i++) {
        u8* data = NULL;
        u32 size = 0;
        snprintf(path, sizeof(path), "%s/%s", base, s_grammar_blobs[i].filename);
        if (!load_file(path, &data, &size)) continue;
        if (*total + size > PC_LANG_MAX_TOTAL_SIZE) { free(data); continue; }
        s_grammar_blobs[i].data = data;
        s_grammar_blobs[i].size = size;
        *total += size;
    }
    for (i = 0; i < sizeof(s_name_blocks) / sizeof(s_name_blocks[0]); i++) {
        PCLanguageNameBlock* block = &s_name_blocks[i];
        u8* data = NULL;
        u32 size = 0;
        snprintf(path, sizeof(path), "%s/%s", base, block->name_filename);
        if (!load_file(path, &data, &size)) continue;
        if (size != block->target_size || *total + size > PC_LANG_MAX_TOTAL_SIZE) {
            printf("[Language] Ignoring invalid item table %s\n", block->name_filename);
            free(data);
            continue;
        }
        memcpy(block->target, data, size);
        free(data);
        *total += size;
        s_item_file_count++;
    }
}

static int pack_grammar(u8 definite_article, u8 indefinite_article, u8 grammar_class) {
    return PC_LANG_GRAMMAR_MAGIC | ((int)definite_article << 16) |
           ((int)indefinite_article << 8) | (int)grammar_class;
}

static int name_record_matches(const u8* record, const u8* str, int len) {
    int i;
    if (!record || !str || len <= 0) return 0;
    for (i = 0; i < (int)PC_LANG_ITEM_NAME_LEN; i++) {
        u8 actual = i < len ? str[i] : 0x20;
        if (record[i] != actual) return 0;
    }
    return 1;
}

static int grammar_choice_from_class(int packed, int alternatives) {
    int grammar_class;
    if (alternatives <= 1) return 0;
    if (!pc_language_grammar_is_packed(packed)) {
        return (strcmp(s_code, "de") == 0) ? 0 : alternatives - 1;
    }
    grammar_class = packed & 0xFF;
    if (grammar_class == 0xFF) {
        return (strcmp(s_code, "de") == 0) ? 0 : alternatives - 1;
    }
    if (strcmp(s_code, "de") == 0) {
        if (grammar_class == 2) return 0; /* masculine */
        if (grammar_class == 3 || grammar_class == 1) return alternatives > 1 ? 1 : 0; /* feminine/plural */
        if (grammar_class == 4) return alternatives > 2 ? 2 : 0; /* neuter */
        return 0;
    }
    /* Spanish, French and Italian PAL grammar classes encode masculine/plural
     * variants as even values and feminine variants as odd values. */
    return (grammar_class & 1) && alternatives > 1 ? 1 : 0;
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
    clear_item_language();
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
    snprintf(base, sizeof(base), "languages/%s/items", s_code);
    load_item_language(base, &total);
    assign_virtual_addresses();

    for (i = 0; i < sizeof(s_resources) / sizeof(s_resources[0]); i++) {
        if (s_resources[i].enabled) enabled_count++;
    }
    if (enabled_count == 0 && s_item_file_count == 0) {
        printf("[Language] No valid resources found for '%s'; using English ROM data\n", s_code);
        return;
    }
    s_external = 1;
    printf("[Language] Loaded '%s': %d ARAM resources, %d item tables, %u bytes; missing resources fall back to English\n",
           s_code, enabled_count, s_item_file_count, total);
}

void pc_language_shutdown(void) {
    clear_resources();
    clear_item_language();
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


int pc_language_grammar_is_packed(int value) {
    return (value & PC_LANG_GRAMMAR_MASK) == PC_LANG_GRAMMAR_MAGIC;
}

int pc_language_strip_grammar_prefix(const u8** str, int* len) {
    const u8* p;
    int packed;
    if (!s_external || !str || !*str || !len || *len < 8) return PC_LANGUAGE_GRAMMAR_NONE;
    p = *str;
    if (p[0] != 0x7F || p[1] != PC_LANG_CUSTOM_CMD || p[2] != 8 ||
        p[3] != 0x0A || p[4] != 0) {
        return PC_LANGUAGE_GRAMMAR_NONE;
    }
    packed = pack_grammar(p[5], p[6], p[7]);
    *str += 8;
    *len -= 8;
    return packed;
}

int pc_language_grammar_for_name(const u8* str, int len) {
    size_t i;
    if (!s_external || !str) return PC_LANGUAGE_GRAMMAR_NONE;
    for (i = 0; i < sizeof(s_name_blocks) / sizeof(s_name_blocks[0]); i++) {
        PCLanguageNameBlock* block = &s_name_blocks[i];
        PCLanguageBlob* grammar = find_grammar_blob(block->grammar_filename);
        u32 count = block->target_size / PC_LANG_ITEM_NAME_LEN;
        u32 idx;
        if (!grammar || !grammar->data) continue;
        for (idx = 0; idx < count; idx++) {
            u32 grammar_offset = block->grammar_offset + idx * 3u;
            if (grammar_offset + 3u > grammar->size) break;
            if (name_record_matches(block->target + idx * PC_LANG_ITEM_NAME_LEN, str, len)) {
                const u8* g = grammar->data + grammar_offset;
                return pack_grammar(g[0], g[1], g[2]);
            }
        }
    }
    return PC_LANGUAGE_GRAMMAR_NONE;
}

int pc_language_expand_custom_control(u8* data, int idx, int len,
                                      int free_grammar, int item_grammar,
                                      int player_is_female) {
    int total;
    int selector;
    int alternatives;
    int choice = 0;
    int lengths_start;
    int payload_start;
    int payload_offset = 0;
    int chosen_len = 0;
    int i;
    if (!data || idx < 0 || idx + 5 > len || data[idx] != 0x7F ||
        data[idx + 1] != PC_LANG_CUSTOM_CMD) return len;
    total = data[idx + 2];
    selector = data[idx + 3];
    alternatives = data[idx + 4];
    if (total < 5 || idx + total > len || alternatives < 0 || alternatives > 32) return len;
    lengths_start = idx + 5;
    payload_start = lengths_start + alternatives;
    if (payload_start > idx + total) return len;

    if (alternatives > 0) {
        if (selector == 0x14) {
            choice = player_is_female && alternatives > 1 ? 1 : 0;
        } else if (selector >= 0x1E && selector <= 0x31) {
            choice = grammar_choice_from_class(free_grammar, alternatives);
        } else if (selector >= 0x32 && selector <= 0x36) {
            choice = grammar_choice_from_class(item_grammar, alternatives);
        }
        if (choice < 0 || choice >= alternatives) choice = 0;
        for (i = 0; i < alternatives; i++) {
            int part_len = data[lengths_start + i];
            if (payload_start + payload_offset + part_len > idx + total) return len;
            if (i == choice) chosen_len = part_len;
            else if (i < choice) payload_offset += part_len;
        }
        if (chosen_len > 0) {
            memmove(data + idx, data + payload_start + payload_offset, (size_t)chosen_len);
        }
    }
    memmove(data + idx + chosen_len, data + idx + total, (size_t)(len - idx - total));
    return len - total + chosen_len;
}

const char* pc_language_code(void) {
    return s_code;
}

int pc_language_is_external(void) {
    return s_external;
}
