#include "table.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <uthash.h>

/* ===== Index UTHASH ===== */
typedef struct topic_map_s {
  char     *topic;   /* key */
  entry_t  *entry;
  UT_hash_handle hh;
} topic_map_t;

typedef struct id_map_s {
  uint32_t  can_id;  /* key */
  entry_t  *entry;
  UT_hash_handle hh;
} id_map_t;

/* ===== Helpers ===== */
static field_type_t parse_ftype(const char *s){
  if(!s) return FT_INT;
  if(strcmp(s,"int")==0)   return FT_INT;
  if(strcmp(s,"bool")==0)  return FT_BOOL;
  if(strcmp(s,"hex")==0)   return FT_HEX;
  if(strcmp(s,"int16")==0) return FT_INT16;
  if(strcmp(s,"dict")==0)  return FT_ENUM;   /* compat */
  if(strcmp(s,"enum")==0)  return FT_ENUM;   /* compat */
  return FT_INT;
}

static void free_enum_list(enum_kv_t *p){
  while(p){ enum_kv_t *n=p->next; free(p->key); free(p); p=n; }
}

static void free_entry(entry_t *e){
  if(!e) return;
  free(e->topic);
  if(e->fields){
    for(size_t i=0;i<e->field_count;i++){
      free(e->fields[i].name);
      if(e->fields[i].enum_list) free_enum_list(e->fields[i].enum_list);
    }
    free(e->fields);
  }
  free(e);
}

static bool entry_push_field(entry_t *e, const char *name, field_type_t t, cJSON *jdict){
  size_t newn = e->field_count + 1;
  field_spec_t *nf = realloc(e->fields, newn * sizeof(field_spec_t));
  if(!nf) return false;
  e->fields = nf;

  field_spec_t *fs = &e->fields[e->field_count];
  fs->name = strdup(name);
  fs->type = t;
  fs->enum_list = NULL;

  if (t == FT_ENUM && jdict && cJSON_IsObject(jdict)) {
    cJSON *it=NULL; enum_kv_t **tail=&fs->enum_list;
    cJSON_ArrayForEach(it, jdict){
      if(!cJSON_IsNumber(it)) continue;
      enum_kv_t *kv = calloc(1,sizeof(enum_kv_t));
      kv->key   = strdup(it->string);
      kv->value = (int)it->valuedouble;
      *tail = kv; tail = &kv->next;
    }
  }
  e->field_count = newn;
  return true;
}

/* data = objet: clé=nom, valeur= "int"/"bool"/"hex"/"int16" ou {enum} */
static bool parse_data_object_into_entry(cJSON *jdata, entry_t *e){
  if (!cJSON_IsObject(jdata)) return false;
  cJSON *it = NULL;
  cJSON_ArrayForEach(it, jdata){
    const char *fname = it->string;
    if (!fname) continue;

    if (cJSON_IsString(it)) {
      /* "fname": "int" */
      field_type_t ft = parse_ftype(it->valuestring);
      if (!entry_push_field(e, fname, ft, NULL)) return false;
    } else if (cJSON_IsObject(it)) {
      /* "mode": { "ON":1, "OFF":2, ... }  => ENUM */
      if (!entry_push_field(e, fname, FT_ENUM, it)) return false;
    } else {
      /* non supporté => ignore poliment */
      LOGW("Champ ignoré (type non supporté) : %s", fname);
    }
  }
  return true;
}

/* Détecte une entrée valide: a "arbitration_id" (ou alias), "topic" et "data" (objet), puis la construit */
static entry_t* try_build_entry_from_block(cJSON *blk){
  if(!cJSON_IsObject(blk)) return NULL;

  cJSON *jtopic = cJSON_GetObjectItemCaseSensitive(blk, "topic");
  cJSON *jid    = cJSON_GetObjectItemCaseSensitive(blk, "arbitration_id");
  if(!cJSON_IsString(jtopic) || !(cJSON_IsNumber(jid) || cJSON_IsString(jid))) return NULL;

  /* ID numérique (décimal) ou texte (0x...) */
  uint32_t can_id = 0;
  if (cJSON_IsNumber(jid)) can_id = (uint32_t)jid->valuedouble;
  else                     can_id = (uint32_t)strtoul(jid->valuestring, NULL, 0);

  cJSON *jdata  = cJSON_GetObjectItemCaseSensitive(blk, "data");
  if(!jdata || !cJSON_IsObject(jdata)) return NULL; /* on veut un objet (peut être vide) */

  entry_t *e = calloc(1, sizeof(entry_t));
  e->topic = strdup(jtopic->valuestring);
  e->can_id = can_id;
  e->fields = NULL;
  e->field_count = 0;

  /* data peut être vide {} : OK (0 champ) */
  if (!parse_data_object_into_entry(jdata, e)) {
    free_entry(e);
    return NULL;
  }
  return e;
}

/* Parcourt récursivement l'objet racine (rfid/sensors/... -> init/update/...) et collecte les entrées */
static void walk_and_collect(cJSON *node, table_t *t, int *topics, int *ids){
  if (!node) return;

  if (cJSON_IsObject(node)) {
    /* 1) Essaye de le traiter comme "bloc d'entrée" */
    entry_t *e = try_build_entry_from_block(node);
    if (e) {
      topic_map_t *tm = calloc(1,sizeof(topic_map_t));
      tm->topic = strdup(e->topic); tm->entry = e;
      HASH_ADD_KEYPTR(hh, t->topic_index, tm->topic, strlen(tm->topic), tm); (*topics)++;

      id_map_t *im = calloc(1,sizeof(id_map_t));
      im->can_id = e->can_id; im->entry = e;
      HASH_ADD(hh, t->id_index, can_id, sizeof(uint32_t), im); (*ids)++;
      return; /* ne pas descendre plus : on a consommé ce bloc */
    }

    /* 2) Sinon, descendre dans ses enfants (rfid, sensors, ... puis init/update...) */
    cJSON *it=NULL;
    cJSON_ArrayForEach(it, node){
      walk_and_collect(it, t, topics, ids);
    }
    return;
  }

  if (cJSON_IsArray(node)) {
    cJSON *it=NULL;
    cJSON_ArrayForEach(it, node){
      walk_and_collect(it, t, topics, ids);
    }
    return;
  }

  /* autres types: rien à faire */
}

/* ===== API ===== */
bool table_load(table_t *t, const char *path_json){
  t->topic_index = NULL; t->id_index = NULL;

  FILE *f = fopen(path_json,"rb");
  if(!f){ LOGE("Ouvrir %s: %s", path_json, strerror(errno)); return false; }
  if(fseek(f,0,SEEK_END)!=0){ fclose(f); LOGE("fseek fin"); return false; }
  long sz = ftell(f);
  if(sz<0){ fclose(f); LOGE("ftell"); return false; }
  if(fseek(f,0,SEEK_SET)!=0){ fclose(f); LOGE("fseek debut"); return false; }

  char *buf = malloc((size_t)sz+1);
  size_t rd = fread(buf,1,(size_t)sz,f); fclose(f);
  if(rd!=(size_t)sz){ free(buf); LOGE("fread incomplet"); return false; }
  buf[sz]=0;

  cJSON *root = cJSON_Parse(buf); free(buf);
  if(!root){ LOGE("JSON invalide"); return false; }

  int topics=0, ids=0;
  walk_and_collect(root, t, &topics, &ids);
  cJSON_Delete(root);

  LOGI("Table chargée: %d topics, %d IDs", topics, ids);
  return (topics>0 && ids>0);
}

void table_free(table_t *t){
  if(!t) return;
  topic_map_t *tm,*tm_tmp;
  HASH_ITER(hh, t->topic_index, tm, tm_tmp){
    HASH_DEL(t->topic_index, tm);
    free(tm->topic);
    free(tm);
  }
  t->topic_index = NULL;

  id_map_t *im,*im_tmp;
  HASH_ITER(hh, t->id_index, im, im_tmp){
    HASH_DEL(t->id_index, im);
    free_entry(im->entry);
    free(im);
  }
  t->id_index = NULL;
}

const entry_t* table_find_by_topic(const table_t *t, const char *topic){
  topic_map_t *tm=NULL;
  HASH_FIND_STR(t->topic_index, topic, tm);
  return tm ? tm->entry : NULL;
}

const entry_t* table_find_by_id(const table_t *t, uint32_t can_id){
  id_map_t *im=NULL;
  HASH_FIND(hh, t->id_index, &can_id, sizeof(uint32_t), im);
  return im ? im->entry : NULL;
}
