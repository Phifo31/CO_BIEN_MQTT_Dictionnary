/**
 * @file pack.c
 * @brief Conversion des données entre format JSON et trame CAN (8 octets).
 *
 * Ce module contient les fonctions responsables de la transformation :
 * - du format JSON (utilisé par MQTT) vers le format binaire CAN (pack)
 * - et inversement (unpack).
 *
 * Il s’appuie sur la table de conversion chargée depuis `conversion.json`,
 * qui indique le type de chaque champ (int, bool, hex, enum…).
 */

#include "pack.h"
#include "log.h"
#include "table.h"      
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*                              Fonctions utilitaires                         */
/* -------------------------------------------------------------------------- */


/**
 * @brief Sature une valeur entière entre 0 et 255.
 * @param x Valeur d’entrée.
 * @return Valeur tronquée sur 8 bits.
 */

static inline uint8_t clamp_u8(int x){ if(x<0) return 0; if(x>255) return 255; return (uint8_t)x; }


/**
 * @brief Convertit une couleur hexadécimale "#RRGGBB" en trois octets RGB.
 * @param s Chaîne d’entrée (format "#RRGGBB").
 * @param[out] rgb Tableau de 3 octets pour stocker les composantes.
 * @return true si la conversion a réussi, false sinon.
 */

static bool parse_hex_rgb(const char *s, uint8_t rgb[3]){
  if(!s || s[0] != '#' || strlen(s) != 7) return false;
  for(int i=0;i<3;i++){
    char buf[3] = { s[1+2*i], s[2+2*i], 0 };
    char *end=NULL; long v = strtol(buf,&end,16);
    if(end==buf || v<0 || v>255) return false;
    rgb[i] = (uint8_t)v;
  }
  return true;
}


/**
 * @brief Convertit une valeur texte d’un enum en code numérique.
 *
 * Exemple : "ON" → 1 (si défini ainsi dans conversion.json)
 *
 * @param fs Description du champ (avec sa liste d’enums).
 * @param s Chaîne à convertir.
 * @param[out] code Code numérique correspondant.
 * @return true si trouvé, false sinon.
 */

static bool enum_str_to_code(const field_spec_t *fs, const char *s, uint8_t *code){
  if(!fs || fs->type!=FT_ENUM || !s) return false;
  for(enum_kv_t *kv=fs->enum_list; kv; kv=kv->next){
    if(kv->key && strcmp(kv->key,s)==0){ *code=(uint8_t)(kv->value & 0xFF); return true; }
  }
  return false;
}

/**
 * @brief Convertit un code numérique d’un enum en texte.
 *
 * Exemple : 1 → "ON"
 *
 * @param fs Description du champ.
 * @param code Code entier.
 * @return Nom du champ correspondant, ou NULL si non trouvé.
 */

static const char* enum_code_to_str(const field_spec_t *fs, uint8_t code){
  if(!fs || fs->type!=FT_ENUM) return NULL;
  for(enum_kv_t *kv=fs->enum_list; kv; kv=kv->next){
    if((kv->value & 0xFF) == code) return kv->key;
  }
  return NULL;
}


/* -------------------------------------------------------------------------- */
/*                              Fonction de "pack"                            */
/* -------------------------------------------------------------------------- */


/**
 * @brief Convertit un objet JSON en tableau de 8 octets CAN.
 *
 * Cette fonction transforme chaque champ du JSON (int, bool, hex, etc.)
 * selon le type défini dans la table.  
 * Les valeurs sont ensuite placées dans le tableau de 8 octets à envoyer
 * sur le bus CAN.
 *
 * @param[out] out8 Tableau de 8 octets à remplir.
 * @param entry Structure décrivant le message (topic, champs, types).
 * @param json_in Objet JSON d’entrée.
 * @return true si la conversion a réussi, false sinon.
 */

bool pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in){
  memset(out8,0,8);
  if(!entry || !json_in) return false;
  size_t idx=0;

  for(size_t i=0;i<entry->field_count;i++){
    const field_spec_t *fs = &entry->fields[i];
    cJSON *v = cJSON_GetObjectItemCaseSensitive(json_in, fs->name);
    if(!v){
      LOGW("Champ manquant: %s", fs->name);
      return false;
    }
    switch(fs->type){
      case FT_INT:{
        if(!cJSON_IsNumber(v)) { LOGW("Type int attendu pour %s", fs->name); return false; }
        if(idx+1>8) return false;
        int x = (int)v->valuedouble;
        if(x<0 || x>255){ LOGW("Valeur %s hors plage: %d", fs->name, x); return false; }
        out8[idx++] = (uint8_t)x;
      }break;
      case FT_BOOL:{
        if(!cJSON_IsBool(v)) { LOGW("Type bool attendu pour %s", fs->name); return false; }
        if(idx+1>8) return false;
        out8[idx++] = cJSON_IsTrue(v) ? 1 : 0;
      }break;
      case FT_HEX:{
        if(!cJSON_IsString(v)) { LOGW("Type hex(#RRGGBB) attendu pour %s", fs->name); return false; }
        if(idx+3>8) return false;
        uint8_t rgb[3];
        if(!parse_hex_rgb(v->valuestring, rgb)){ LOGW("Format hex invalide pour %s", fs->name); return false; }
        out8[idx++]=rgb[0]; out8[idx++]=rgb[1]; out8[idx++]=rgb[2];
      }break;
      case FT_INT16:{
        if(!cJSON_IsNumber(v)){ LOGW("Type int16 attendu pour %s", fs->name); return false; }
        if(idx+2>8) return false;
        int x = (int)v->valuedouble;
        if(x<0 || x>65535){ LOGW("Valeur %s hors plage: %d", fs->name, x); return false; }
        out8[idx++] = (uint8_t)((x>>8)&0xFF);
        out8[idx++] = (uint8_t)(x & 0xFF);
      }break;
      case FT_ENUM:{
        if(!cJSON_IsString(v)){ LOGW("Type enum(string) attendu pour %s", fs->name); return false; }
        if(idx+1>8) return false;
        uint8_t code=0;
        if(!enum_str_to_code(fs, v->valuestring, &code)){
          LOGW("Valeur enum inconnue '%s' pour %s", v->valuestring, fs->name);
          return false;
        }
        out8[idx++] = code;
      }break;
    }
  }
   /* Les octets restants sont à 0 par défaut */
  return true;
}


/* -------------------------------------------------------------------------- */
/*                              Fonction de "unpack"                          */
/* -------------------------------------------------------------------------- */


/**
 * @brief Convertit une trame CAN (8 octets) en objet JSON.
 *
 * Cette fonction fait l’opération inverse de `pack_payload()` :
 * elle lit les 8 octets d’une trame CAN et reconstruit un
 * objet JSON lisible pour MQTT.
 *
 * @param in8 Tableau d’octets CAN reçu.
 * @param entry Structure décrivant le message attendu.
 * @return Objet JSON reconstruit, ou NULL en cas d’erreur.
 */

cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry){
  if(!entry) return NULL;
  cJSON *obj = cJSON_CreateObject();
  if(!obj) return NULL;
  size_t idx=0;

  for(size_t i=0;i<entry->field_count;i++){
    const field_spec_t *fs = &entry->fields[i];
    switch(fs->type){
      case FT_INT:{
        if(idx+1>8) goto fail;
        cJSON_AddNumberToObject(obj, fs->name, (int)in8[idx++]);
      }break;
      case FT_BOOL:{
        if(idx+1>8) goto fail;
        cJSON_AddBoolToObject(obj, fs->name, in8[idx++] ? 1:0);
      }break;
      case FT_HEX:{
        if(idx+3>8) goto fail;
        char buf[8]; snprintf(buf,sizeof(buf),"#%02X%02X%02X", in8[idx],in8[idx+1],in8[idx+2]);
        cJSON_AddStringToObject(obj, fs->name, buf); idx+=3;
      }break;
      case FT_INT16:{
        if(idx+2>8) goto fail;
        int v = ((int)in8[idx]<<8) | (int)in8[idx+1]; idx+=2;
        cJSON_AddNumberToObject(obj, fs->name, v);
      }break;
      case FT_ENUM:{
        if(idx+1>8) goto fail;
        uint8_t code = in8[idx++];
        const char *s = enum_code_to_str(fs, code);
        if(s) cJSON_AddStringToObject(obj, fs->name, s);
        else  cJSON_AddNumberToObject(obj, fs->name, (int)code);
      }break;
    }
  }
  return obj;
fail:
  cJSON_Delete(obj);
  return NULL;
}
