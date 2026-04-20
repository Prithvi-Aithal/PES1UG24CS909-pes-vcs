#include "pes.h"
#include "index.h"
#include "commit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Forward declarations
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

void cmd_init(void) {
    mkdir(PES_DIR,     0755);
    mkdir(OBJECTS_DIR, 0755);
    mkdir(".pes/refs", 0755);
    mkdir(REFS_DIR,    0755);
    FILE *f = fopen(HEAD_FILE, "w");
    if (f) { fprintf(f, "ref: refs/heads/main\n"); fclose(f); }
    printf("Initialized empty PES repository in .pes/\n");
}

void cmd_add(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: pes add <file>...\n"); return; }
    Index index;
    index.count = 0;
    index_load(&index);
    for (int i = 2; i < argc; i++) {
        if (index_add(&index, argv[i]) != 0)
            fprintf(stderr, "error: failed to add '%s'\n", argv[i]);
    }
}

void cmd_status(void) {
    Index index;
    index.count = 0;
    index_load(&index);
    index_status(&index);
}

static void log_callback(const ObjectID *id, const Commit *c, void *ctx) {
    (void)ctx;
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    printf("commit %s\n", hex);
    printf("Author: %s\n", c->author);
    printf("Date:   %llu\n", (unsigned long long)c->timestamp);
    printf("\n    %s\n\n", c->message);
}

void cmd_log(void) {
    if (commit_walk(log_callback, NULL) != 0)
        fprintf(stderr, "error: no commits yet\n");
}

void cmd_commit(int argc, char *argv[]) {
    const char *msg = NULL;
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], "-m") == 0) { msg = argv[i+1]; break; }
    }
    if (!msg) {
        fprintf(stderr, "error: commit requires a message (-m \"message\")\n");
        return;
    }
    ObjectID id;
    if (commit_create(msg, &id) != 0) {
        fprintf(stderr, "error: commit failed\n");
        return;
    }
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&id, hex);
    // Phase 4: print first 12 chars of hash as short commit id
    printf("Committed: %.12s... %s\n", hex, msg);
}

void cmd_branch(int argc, char *argv[]) { (void)argc; (void)argv; }
void cmd_checkout(int argc, char *argv[]) { (void)argc; (void)argv; }

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pes <command> [args]\n");
        return 1;
    }
    const char *cmd = argv[1];
    if      (strcmp(cmd, "init")     == 0) cmd_init();
    else if (strcmp(cmd, "add")      == 0) cmd_add(argc, argv);
    else if (strcmp(cmd, "status")   == 0) cmd_status();
    else if (strcmp(cmd, "commit")   == 0) cmd_commit(argc, argv);
    else if (strcmp(cmd, "log")      == 0) cmd_log();
    else if (strcmp(cmd, "branch")   == 0) cmd_branch(argc, argv);
    else if (strcmp(cmd, "checkout") == 0) cmd_checkout(argc, argv);
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return 1;
    }
    return 0;
}
