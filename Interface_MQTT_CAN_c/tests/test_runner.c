// tests/test_runner.c
// Lanceur simple pour exécuter les tests compilés (binaires) un par un.
// Active/désactive des blocs en commentant/décommentant les #define ci-dessous.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

// ======= Sélection des tests à lancer =======
#define RUN_TEST_TABLE         1
#define RUN_TEST_PACK          1
#define RUN_TEST_MQTT_FILTER   1
#define RUN_TEST_MQTT_PUBLISH  1
// ============================================

// Emplacements possibles des binaires (selon où tu lances le runner).
// On essaie dans l'ordre jusqu’à trouver le binaire.
static const char *CANDIDATE_PREFIXES[] = {
    "build/tests/",       // si tu lances depuis la racine du projet
    "./",                 // si tu te places dans build/tests/ avant
    "tests/",             // (au cas où)
    NULL
};

static bool run_one(const char *prog_name) {
    char fullpath[512];

    for (int i = 0; CANDIDATE_PREFIXES[i]; ++i) {
        snprintf(fullpath, sizeof(fullpath), "%s%s", CANDIDATE_PREFIXES[i], prog_name);
        if (access(fullpath, X_OK) == 0) {
            printf("\n==============================\n");
            printf(">>> RUN: %s\n", fullpath);
            printf("==============================\n");

            pid_t pid = fork();
            if (pid == 0) {
                // enfant : exécute le test
                execl(fullpath, fullpath, (char*)NULL);
                // si execl échoue:
                perror("execl");
                _exit(127);
            } else if (pid > 0) {
                int status = 0;
                if (waitpid(pid, &status, 0) < 0) {
                    perror("waitpid");
                    return false;
                }
                if (WIFEXITED(status)) {
                    int rc = WEXITSTATUS(status);
                    printf("<<< END %s (exit=%d)\n", prog_name, rc);
                    return rc == 0; // 0 = succès pour cmocka
                } else if (WIFSIGNALED(status)) {
                    printf("<<< END %s (signal=%d)\n", prog_name, WTERMSIG(status));
                    return false;
                }
                return false;
            } else {
                perror("fork");
                return false;
            }
        }
    }

    printf("SKIP: binaire introuvable pour %s (as-tu compilé ?)\n", prog_name);
    return false;
}

int main(void) {
    int passed = 0, total = 0;

#if RUN_TEST_TABLE
    total++;
    if (run_one("test_table")) passed++;
#endif

#if RUN_TEST_PACK
    total++;
    if (run_one("test_pack")) passed++;
#endif

#if RUN_TEST_MQTT_FILTER
    total++;
    if (run_one("test_mqtt_filter")) passed++;
#endif

#if RUN_TEST_MQTT_PUBLISH
    total++;
    if (run_one("test_mqtt_publish")) passed++;
#endif

    printf("\n==============================\n");
    printf("RÉSUMÉ: %d/%d tests OK\n", passed, total);
    printf("==============================\n");
    return (passed == total) ? 0 : 1;
}
