#ifndef PACK_H
#define PACK_H


/* Packe un objet JSON vers 8 octets CAN selon l'entry. */
bool pack_payload(uint8_t out8[8], const entry_t *entry, cJSON *json_in);

/* Dépacke 8 octets CAN vers un objet JSON (à libérer avec cJSON_Delete). */
cJSON* unpack_payload(const uint8_t in8[8], const entry_t *entry);

#endif /* PACK_H */

// End of file
