#include "bridge_app.h"
#include "table.h"
#include "mqtt_io.h"
#include "can_io.h"
#include "log.h"

#include <mosquitto.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>



// --- paramètres fixes par défaut ---
static const char *IFNAME    = "can0";        // ou "vcan0" en test logiciel
static const char *MQTT_HOST = "localhost";
static const int   MQTT_PORT = 1883;

static volatile int g_running = 1;
static table_t    g_table;
static mqtt_ctx_t g_mqtt;
static can_ctx_t  g_can;

static void on_sig(int s){ (void)s; g_running = 0; }


/* ======================================================
   SETUP
   ====================================================== */
bool my_setup(const char *cfg_path) {
    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    memset(&g_table, 0, sizeof(g_table));
    memset(&g_mqtt,  0, sizeof(g_mqtt));
    memset(&g_can,   0, sizeof(g_can));

    if (!table_load(&g_table, cfg_path)) {
        LOGE("Echec chargement table: %s", cfg_path);
        return false;
    }

    if (!mqtt_init(&g_mqtt, MQTT_HOST, MQTT_PORT, 60))
        return false;

    mqtt_set_qos(&g_mqtt, 1, 1);

    // Lier la callback MQTT -> CAN avec userdata (table+can+mqtt)
    struct { const table_t *t; can_ctx_t *c; mqtt_ctx_t *m; } *ub =
        malloc(sizeof(*ub));
    if (!ub) return false;

    ub->t = &g_table; ub->c = &g_can; ub->m = &g_mqtt;
    mqtt_set_user_data(&g_mqtt, ub);

    if (!mqtt_subscribe_all(&g_mqtt)) return false;
    if (!can_init(&g_can, IFNAME)) return false;

    LOGI("Setup OK (cfg=%s, if=%s, mqtt=%s:%d)", cfg_path, IFNAME, MQTT_HOST, MQTT_PORT);
    return true;
}

/* ======================================================
   LOOP
   ====================================================== */
bool my_loop(void)
{
    if (!g_running) return false;

    if (g_mqtt.mosq)
        mosquitto_loop(g_mqtt.mosq, 0, 100);

    can_poll(&g_can, &g_table, &g_mqtt, 8);

    // usleep(1000); // 1 ms
    return true;
}

/* ======================================================
   SHUTDOWN
   ====================================================== */
void my_shutdown(void) {
    void *ud = g_mqtt.mosq ? mosquitto_userdata(g_mqtt.mosq) : NULL;
    if (ud) free(ud);

    can_cleanup(&g_can);
    mqtt_cleanup(&g_mqtt);
    table_free(&g_table);
    LOGI("Shutdown OK");
}

/* ======================================================
   MAIN
   ====================================================== */
int main(int argc, char **argv)
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/conversion.json";

    if (!my_setup(cfg_path))
        return 1;

    while (my_loop())
        ; // boucle principale

    my_shutdown();
    return 0;
}


