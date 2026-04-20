#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++)
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int rem = index->count - i - 1;
            if (rem > 0)
                memmove(&index->entries[i], &index->entries[i+1],
                        rem * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int n = 0;
    for (int i = 0; i < index->count; i++) {
        printf("    staged: %s\n", index->entries[i].path);
        n++;
    }
    if (!n) printf("    (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    n = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("    deleted: %s\n", index->entries[i].path); n++;
        } else if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                   st.st_size  != (off_t)index->entries[i].size) {
            printf("    modified: %s\n", index->entries[i].path); n++;
        }
    }
    if (!n) printf("    (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    n = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            if (strcmp(ent->d_name, "pes") == 0) continue;
            if (strstr(ent->d_name, ".o") != NULL) continue;
            if (strcmp(ent->d_name, "test_tree") == 0) continue;
            if (strcmp(ent->d_name, "test_objects") == 0) continue;
            int tracked = 0;
            for (int i = 0; i < index->count; i++)
                if (strcmp(index->entries[i].path, ent->d_name) == 0)
                    { tracked = 1; break; }
            if (!tracked) {
                struct stat st;
                if (stat(ent->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                    printf("    untracked: %s\n", ent->d_name); n++;
                }
            }
        }
        closedir(dir);
    }
    if (!n) printf("    (nothing to show)\n");
    printf("\n");
    return 0;
}

static int cmp_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry*)a)->path, ((const IndexEntry*)b)->path);
}

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    char line[800];
    while (fgets(line, sizeof(line), f)) {
        unsigned int mode;
        char hex[65];
        unsigned long long mtime, size;
        char path[512];
        if (sscanf(line, "%o %64s %llu %llu %511s",
                   &mode, hex, &mtime, &size, path) != 5) continue;
        IndexEntry *e = &index->entries[index->count];
        e->mode      = mode;
        e->mtime_sec = (uint64_t)mtime;
        e->size      = (uint32_t)size;
        memcpy(e->path, path, 512);
        e->path[511] = '\0';
        if (hex_to_hash(hex, &e->hash) != 0) { fclose(f); return -1; }
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    Index *sorted = malloc(sizeof(Index));
    if (!sorted) return -1;
    *sorted = *index;
    qsort(sorted->entries, sorted->count, sizeof(IndexEntry), cmp_entries);
    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) { perror("index_save"); free(sorted); return -1; }
    for (int i = 0; i < sorted->count; i++) {
        char hex[65];
        hash_to_hex(&sorted->entries[i].hash, hex);
        fprintf(f, "%o %s %llu %u %s\n",
                sorted->entries[i].mode, hex,
                (unsigned long long)sorted->entries[i].mtime_sec,
                sorted->entries[i].size,
                sorted->entries[i].path);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    free(sorted);
    return rename(".pes/index.tmp", INDEX_FILE);
}

int index_add(Index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize < 0) { fclose(f); return -1; }
    unsigned char *data = malloc(fsize + 1);
    if (!data) { fclose(f); return -1; }
    size_t r = fread(data, 1, fsize, f);
    (void)r;
    fclose(f);
    ObjectID id;
    int rc = object_write(OBJ_BLOB, data, (size_t)fsize, &id);
    free(data);
    if (rc != 0) { fprintf(stderr, "error: object_write failed\n"); return -1; }
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        entry = &index->entries[index->count++];
    }
    entry->hash      = id;
    entry->mode      = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    entry->mtime_sec = (uint64_t)st.st_mtime;
    entry->size      = (uint32_t)st.st_size;
    memcpy(entry->path, path, 512);
    entry->path[511] = '\0';
    return index_save(index);
}
