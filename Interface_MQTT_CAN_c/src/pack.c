#include "pack.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>   // snprintf

static inline uint8_t u8(int x) { return (uint8_t)(x & 0xFF); }

/* --- Helpers stricts --- */

// 1 octet strict [0..255]
static bool write_u8(cJSON *obj, const char *key, uint8_t *dst) {
  cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!cJSON_IsNumber(it)) { LOGW("Champ %s non numérique", key); return false; }
  int v = it->valueint;
  if (v < 0 || v > 255) { LOGW("Valeur %s hors plage: %d", key, v); return false; }
  *dst = (uint8_t)v;
  return true;
}

// "#RRGGBB" strict → 3 octets
static bool parse_rgb_hex_strict(const char *s, uint8_t rgb[3]) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  for (int i=1;i<7;i++){
    char c=s[i];
    bool ok = (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
    if(!ok) return false;
  }
  unsigned int rv,gv,bv;
  if (sscanf(s+1, "%2x%2x%2x", &rv, &gv, &bv) != 3) return false;
  rgb[0]=(uint8_t)rv; rgb[1]=(uint8_t)gv; rgb[2]=(uint8_t)bv;
  return true;
}

bool enum_str_to_code(const field_spec_t *fs, const char *s, uint8_t *code) {
  if (!fs || fs->type != FT_ENUM || !s) return false;
  for (enum_kv_t *kv = fs->enum_list; kv; kv = kv->next) {
    if (strcmp(kv->key, s) == 0) {
      *code = u8(kv->value);
      return true;
    }
  }
  return false;
}

const char* enum_code_to_str(const field_spec_t *fs, uint8_t code) {
  if (!fs || fs->type != FT_ENUM) return NULL;
  for (enum_kv_t *kv = fs->enum_list; kv; kv = kv->next) {
    if (kv->value == code) return kv->key;
  }
  return NULL;
}

/* --- Pack / Unpack --- */

bool pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in) {
  memset(out8, 0, 8);
  size_t idx = 0;

  for (size_t i=0; i<entry->field_count; ++i) {
    const field_spec_t *fs = &entry->fields[i];
    cJSON *v = cJSON_GetObjectItemCaseSensitive(json_in, fs->name);
    if (!v) { LOGW("Champ manquant: %s", fs->name); return false; }

    switch (fs->type) {
      case FT_INT: { // 1 octet strict
        if (idx+1>8) return false;
        if (!write_u8(json_in, fs->name, &out8[idx])) return false;
        idx += 1;
      } break;

      case FT_BOOL: {
        if (idx+1>8) return false;
        if (!cJSON_IsBool(v)) { LOGW("Type bool attendu pour %s", fs->name); return false; }
        out8[idx++] = cJSON_IsTrue(v) ? 1 : 0;
      } break;

      case FT_HEX: { // "#RRGGBB" → 3 octets
        if (idx+3>8) return false;
        if (!cJSON_IsString(v)) { LOGW("Type hex(#RRGGBB) attendu pour %s", fs->name); return false; }
        uint8_t rgb[3];
        if (!parse_rgb_hex_strict(v->valuestring, rgb)) { LOGW("Format hex invalide pour %s", fs->name); return false; }
        out8[idx++] = rgb[0];
        out8[idx++] = rgb[1];
        out8[idx++] = rgb[2];
      } break;

      case FT_INT16: { // 2 octets big-endian strict [-32768..32767]
        if (idx+2>8) return false;
        if (!cJSON_IsNumber(v)) { LOGW("Type int16 attendu pour %s", fs->name); return false; }
        int val = (int)v->valueint;
        if (val < -32768 || val > 32767) { LOGW("int16 hors plage pour %s: %d", fs->name, val); return false; }
        uint16_t u = (uint16_t)(val & 0xFFFF);
        out8[idx++] = (uint8_t)((u >> 8) & 0xFF); // BE
        out8[idx++] = (uint8_t)(u & 0xFF);
      } break;

      case FT_ENUM: {
        if (idx+1>8) return false;
        if (!cJSON_IsString(v)) { LOGW("Type enum(string) attendu pour %s", fs->name); return false; }
        uint8_t code;
        if (!enum_str_to_code(fs, v->valuestring, &code)) {
          LOGW("Valeur enum inconnue '%s' pour %s", v->valuestring, fs->name);
          return false;
        }
        out8[idx++] = code;
      } break;
    }
  }

  // padding déjà 0x00 via memset
  return true;
}

cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry) {
  cJSON *obj = cJSON_CreateObject();
  size_t idx = 0;

  for (size_t i=0; i<entry->field_count; ++i) {
    const field_spec_t *fs = &entry->fields[i];

    switch (fs->type) {
      case FT_INT: {
        if (idx+1>8) goto fail;
        cJSON_AddNumberToObject(obj, fs->name, in8[idx++]);
      } break;

      case FT_BOOL: {
        if (idx+1>8) goto fail;
        cJSON_AddBoolToObject(obj, fs->name, in8[idx++] ? 1 : 0);
      } break;

      case FT_HEX: {
        if (idx+3>8) goto fail;
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", in8[idx], in8[idx+1], in8[idx+2]);
        cJSON_AddStringToObject(obj, fs->name, buf);
        idx += 3;
      } break;

      case FT_INT16: {
        if (idx+2>8) goto fail;
        int val = ((int)in8[idx] << 8) | (int)in8[idx+1];
        if (val & 0x8000) val -= 0x10000; // signé
        cJSON_AddNumberToObject(obj, fs->name, val);
        idx += 2;
      } break;

      case FT_ENUM: {
        if (idx+1>8) goto fail;
        uint8_t code = in8[idx++];
        const char *label = enum_code_to_str(fs, code);
        if (!label) cJSON_AddNumberToObject(obj, fs->name, code);
        else        cJSON_AddStringToObject(obj, fs->name, label);
      } break;
    }
  }
  return obj;

fail:
  cJSON_Delete(obj);
  return NULL;
}
