#ifndef TABLE_H
#define TABLE_H


#ifdef __cplusplus
extern "C" {
#endif

bool table_load(table_t *t, const char *json_path);
void table_free(table_t *t);

/* lookups (O(1)) */
const entry_t* table_find_by_topic(const table_t *t, const char *topic);
const entry_t* table_find_by_canid(const table_t *t, uint32_t can_id);

#ifdef __cplusplus
}
#endif

#endif /* TABLE_H */

// End of file
