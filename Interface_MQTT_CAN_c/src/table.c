#include "table.h"
#include "log.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h> 

static char* sdup(const char *s){
  if(!s) return NULL;
  size_t n = strlen(s)+1;
  char *p = (char*)malloc(n);
  if(p) memcpy(p, s, n);
  return p;
}


static enum_kv_t* enum_list_from_obj(cJSON *obj){
  if(!cJSON_IsObject(obj)) return NULL;
  enum_kv_t *head=NULL, *tail=NULL;
  for(cJSON *it = obj->child; it; it=it->next){
    if(!cJSON_IsNumber(it)) continue;
    enum_kv_t *node = (enum_kv_t*)calloc(1,sizeof(enum_kv_t));
    if(!node) break;
    node->key = sdup(it->string);
    node->value = (int)it->valuedouble;
    if(!head) head=tail=node; else { tail->next=node; tail=node; }
  }
  return head;
}
static void enum_list_free(enum_kv_t *kv){
  while(kv){ enum_kv_t *n=kv->next; free(kv->key); free(kv); kv=n; }
}

static field_type_t parse_type(const char *s){
  if(!s) return FT_INT;
  if(!strcasecmp(s,"int"))   return FT_INT;
  if(!strcasecmp(s,"bool") || !strcasecmp(s,"boolean")) return FT_BOOL;
  if(!strcasecmp(s,"hex") || !strcasecmp(s,"rgb"))      return FT_HEX;
  if(!strcasecmp(s,"int16")|| !strcasecmp(s,"u16") || !strcasecmp(s,"uint16")) return FT_INT16;
  if(!strcasecmp(s,"enum") || !strcasecmp(s,"dict"))    return FT_ENUM;
  return FT_INT; /* par défaut */
}

static bool build_fields_from_node(cJSON *data, field_spec_t **out_arr, size_t *out_n){
  *out_arr=NULL; *out_n=0;
  /* Accepte data = array d’objets {name,type[,dict]} ou data = objet {name:type} */
  if(cJSON_IsArray(data)){
    size_t n = cJSON_GetArraySize(data);
    if(n==0) return true;
    field_spec_t *arr = (field_spec_t*)calloc(n, sizeof(field_spec_t));
    if(!arr) return false;
    size_t k=0;
    for(cJSON *it=data->child; it; it=it->next){
      if(!cJSON_IsObject(it)) continue;
      cJSON *jname = cJSON_GetObjectItemCaseSensitive(it,"name");
      cJSON *jtype = cJSON_GetObjectItemCaseSensitive(it,"type");
      if(!cJSON_IsString(jname)||!cJSON_IsString(jtype)) continue;
      arr[k].name = sdup(jname->valuestring);
      arr[k].type = parse_type(jtype->valuestring);
      if(arr[k].type==FT_ENUM){
        cJSON *jdict = cJSON_GetObjectItemCaseSensitive(it,"dict");
        if(!jdict) jdict = cJSON_GetObjectItemCaseSensitive(it,"enum");
        arr[k].enum_list = enum_list_from_obj(jdict);
      }
      k++;
    }
    *out_arr = arr; *out_n = k;
    return true;
  } else if(cJSON_IsObject(data)){
    /* { "field":"int", ... } */
    size_t n=0;
    for(cJSON *it=data->child; it; it=it->next) n++;
    if(n==0) return true;
    field_spec_t *arr = (field_spec_t*)calloc(n, sizeof(field_spec_t));
    if(!arr) return false;
    size_t k=0;
    for(cJSON *it=data->child; it; it=it->next){
      arr[k].name = sdup(it->string);
      arr[k].type = parse_type(cJSON_IsString(it)? it->valuestring : "int");
      arr[k].enum_list = NULL;
      k++;
    }
    *out_arr=arr; *out_n=n; return true;
  }
  return false;
}

bool table_load(table_t *t, const char *json_path){
  if(!t||!json_path) return false;
  memset(t,0,sizeof(*t));

  char *txt = NULL;
  { /* lire fichier */
    FILE *f = fopen(json_path,"rb");
    if(!f){ LOGE("Ouvrir %s", json_path); return false; }
    fseek(f,0,SEEK_END); long L = ftell(f); fseek(f,0,SEEK_SET);
    txt = (char*)malloc((size_t)L+1);
    if(!txt){ fclose(f); return false; }
    if(fread(txt,1,(size_t)L,f)!=(size_t)L){ fclose(f); free(txt); return false; }
    fclose(f); txt[L]='\0';
  }

  cJSON *root = cJSON_Parse(txt); free(txt);
  if(!root){ LOGE("JSON invalide"); return false; }

  /* Collecter toutes les entrées contenant "topic" et "data" */
  size_t cap=16; size_t n=0;
  entry_t *arr = (entry_t*)calloc(cap,sizeof(entry_t));
  if(!arr){ cJSON_Delete(root); return false; }

  /* DFS */
  cJSON *stack[64]; int sp=0; stack[sp++]=root;
  while(sp>0){
    cJSON *node = stack[--sp];
    if(cJSON_IsObject(node)){
      cJSON *jtopic = cJSON_GetObjectItemCaseSensitive(node,"topic");
      cJSON *jdata  = cJSON_GetObjectItemCaseSensitive(node,"data");
      cJSON *jid    = cJSON_GetObjectItemCaseSensitive(node,"arbitration_id");
      if(!jid)      jid = cJSON_GetObjectItemCaseSensitive(node,"id");
      if(jtopic && cJSON_IsString(jtopic) && jdata && jid && cJSON_IsNumber(jid)){
        if(n==cap){ cap*=2; arr=(entry_t*)realloc(arr, cap*sizeof(entry_t)); }
        entry_t *e = &arr[n];
        memset(e,0,sizeof(*e));
        e->topic = sdup(jtopic->valuestring);
        e->can_id= (uint32_t)jid->valuedouble;
        if(!build_fields_from_node(jdata, &e->fields, &e->field_count)){
          LOGW("data invalide pour %s", e->topic);
        } else {
          n++;
        }
      }
      for(cJSON *it=node->child; it; it=it->next){
        if(cJSON_IsObject(it) || cJSON_IsArray(it)) stack[sp++]=it;
      }
    } else if(cJSON_IsArray(node)){
      for(cJSON *it=node->child; it; it=it->next){
        if(cJSON_IsObject(it) || cJSON_IsArray(it)) stack[sp++]=it;
      }
    }
  }

  cJSON_Delete(root);
  t->entries = arr; t->entry_count = n;
  LOGI("Table chargée: %zu topics, %zu IDs", n, n);
  return (n>0);
}

void table_free(table_t *t){
  if(!t||!t->entries) return;
  for(size_t i=0;i<t->entry_count;i++){
    entry_t *e = &t->entries[i];
    free(e->topic);
    if(e->fields){
      for(size_t k=0;k<e->field_count;k++){
        free(e->fields[k].name);
        enum_list_free(e->fields[k].enum_list);
      }
      free(e->fields);
    }
  }
  free(t->entries);
  memset(t,0,sizeof(*t));
}

const entry_t* table_find_by_topic(const table_t *t, const char *topic){
  if(!t||!topic) return NULL;
  for(size_t i=0;i<t->entry_count;i++){
    if(t->entries[i].topic && strcmp(t->entries[i].topic, topic)==0) return &t->entries[i];
  }
  return NULL;
}
const entry_t* table_find_by_canid(const table_t *t, uint32_t can_id){
  if(!t) return NULL;
  for(size_t i=0;i<t->entry_count;i++){
    if(t->entries[i].can_id == can_id) return &t->entries[i];
  }
  return NULL;
}

