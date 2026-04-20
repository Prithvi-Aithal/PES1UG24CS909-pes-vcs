// object.c — Content-addressable object store

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/sha.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(id_out->hash, &ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// Phase 1: object_write builds header + data buffer, computes SHA-256 hash ────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // 1. Pick type string
    const char *type_str;
    if      (type == OBJ_BLOB)   type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // 2. Build header: "blob 16\0"
    char header[64];
    int hlen = snprintf(header, sizeof(header), "%s %zu", type_str, len);

    // 3. Build full object = header + '\0' + data
    size_t full_len = (size_t)hlen + 1 + len;
    unsigned char *full = malloc(full_len);
    if (!full) return -1;
    memcpy(full, header, (size_t)hlen);
    full[hlen] = '\0';
    memcpy(full + hlen + 1, data, len);

    // 4. Compute hash of full object
    compute_hash(full, full_len, id_out);

    // Phase 1: deduplication - same content = same hash = skip write
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // 6. Build paths
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char dir[512], path[512], tmp[520];
    snprintf(dir,  sizeof(dir),  "%s/%.2s",    OBJECTS_DIR, hex);
    snprintf(path, sizeof(path), "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
    snprintf(tmp,  sizeof(tmp),  "%s.tmp",     path);

    // 7. Create shard directory
    mkdir(dir, 0755);

    // 8. Write to temp file
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }

    ssize_t written = write(fd, full, full_len);
    fsync(fd);
    close(fd);
    free(full);

    if (written != (ssize_t)full_len) { unlink(tmp); return -1; }

    // 9. Atomic rename
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }

    // 10. fsync the directory
    int dfd = open(dir, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }

    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // 1. Build path
    char path[512];
    object_path(id, path, sizeof(path));

    // 2. Open and read entire file
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    size_t full_len = (size_t)st.st_size;

    unsigned char *full = malloc(full_len);
    if (!full) { close(fd); return -1; }

    ssize_t got = read(fd, full, full_len);
    close(fd);
    if (got != (ssize_t)full_len) { free(full); return -1; }

    // 3. Verify integrity — recompute hash and compare
    ObjectID computed;
    compute_hash(full, full_len, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(full);
        return -1;
    }

    // 4. Parse header: find '\0'
    unsigned char *null_pos = memchr(full, '\0', full_len);
    if (!null_pos) { free(full); return -1; }

    size_t hlen = (size_t)(null_pos - full);
    char header[128];
    if (hlen >= sizeof(header)) { free(full); return -1; }
    memcpy(header, full, hlen);
    header[hlen] = '\0';

    // 5. Parse type and size from header
    char type_str[16];
    size_t data_size;
    if (sscanf(header, "%15s %zu", type_str, &data_size) != 2) {
        free(full); return -1;
    }

    if      (strcmp(type_str, "blob")   == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree")   == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else { free(full); return -1; }

    // 6. Copy data portion
    *len_out  = data_size;
    *data_out = malloc(data_size);
    if (!*data_out) { free(full); return -1; }
    memcpy(*data_out, null_pos + 1, data_size);

    free(full);
    return 0;
}
