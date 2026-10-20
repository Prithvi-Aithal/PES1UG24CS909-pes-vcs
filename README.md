# PES-VCS: a Git-style version control system in C

PES-VCS is a small version control system built on the same ideas as Git's internals: a SHA-256 content-addressable object store, a staging index, tree objects for directories, and a chain of commits that `log` can walk.

It was built for the Operating Systems course at PES University. The course provided the lab template (CLI, headers and tests). I implemented the object store, tree building, the staging index and commit creation: `object.c`, `tree.c`, `index.c` and `commit.c`.

## Demo

```text
$ pes init
Initialized empty PES repository in .pes/

$ echo "hello world" > hello.txt
$ mkdir src && echo 'int main(void) { return 0; }' > src/main.c
$ pes add hello.txt src/main.c
$ pes status
Staged changes:
    staged: hello.txt
    staged: src/main.c

Unstaged changes:
    (nothing to show)

Untracked files:
    (nothing to show)

$ pes commit -m "Initial commit"
Committed: 00018fdc4214... Initial commit

$ echo "goodbye" > bye.txt && pes add bye.txt && pes commit -m "Add bye.txt"
Committed: 082afffdce20... Add bye.txt

$ pes log
commit 082afffdce20af7a6466e0f5237858ea346fbd416afea62e7f0056fc0ab4ed30
Author: Prithvi Aithal <PES1UG24CS909>
Date:   1792493674

    Add bye.txt

commit 00018fdc421480eb83951edda36a76a02d2a0fb372e0289281a3d5d329fcf048
Author: Prithvi Aithal <PES1UG24CS909>
Date:   1792493674

    Initial commit
```

## How it works

| Piece | File | What it does |
|---|---|---|
| **Object store** | `object.c` | Every object is stored as `"<type> <size>\0<data>"` and named by the SHA-256 of those bytes, under `.pes/objects/<first 2 hex chars>/<remaining 62>`. Identical content gets the same name, so it is stored only once. |
| **Trees** | `tree.c` | A directory snapshot: entries sorted by name, each `<mode> <name>\0<32-byte hash>`, so the same directory always serializes to the same bytes. `tree_from_index` builds nested trees recursively from the staged paths. |
| **Index** | `index.c` | The staging area, a sorted text file (`.pes/index`) with one line per file: mode, blob hash, mtime, size and path. `status` compares it with the working directory. |
| **Commits** | `commit.c` | A commit records its tree, its parent (if any), author, timestamp and message. `HEAD` points at `refs/heads/main`, which holds the latest commit hash, and `log` follows the parent links back to the first commit. |

### Reliability details

- **Atomic writes:** an object is written to a temporary file, `fsync`'d, then `rename`d into place, and the shard directory is `fsync`'d too. A crash can't leave a half-written object under a valid name.
- **Integrity checks on read:** `object_read` re-hashes the file and rejects it if the hash doesn't match its name, so corrupted objects are caught.
- **Deduplication:** before writing, `object_write` checks whether an object with that hash already exists.

## Commands

| Command | Description |
|---|---|
| `pes init` | Create `.pes/` with `objects/`, `refs/heads/` and `HEAD` |
| `pes add <file>...` | Hash files into blobs and stage them in the index |
| `pes status` | Show staged, unstaged and untracked files |
| `pes commit -m "<message>"` | Build trees from the index and record a commit |
| `pes log` | Walk the commit history from `HEAD` |

The author name comes from the `PES_AUTHOR` environment variable (default: `PES User <pes@localhost>`).

## Build and test

Needs Linux, `gcc` and OpenSSL's development headers.

```bash
sudo apt install -y gcc build-essential libssl-dev
make all            # builds pes, test_objects and test_tree
./test_objects      # object store: blob storage, deduplication, integrity check
./test_tree         # trees: serialize/parse round trip, deterministic serialization
./test_sequence.sh  # end-to-end: init, add, commit and log in a temporary directory
```

## Repository layout

```text
pes.h / pes.c        core types, constants and CLI (provided by the course)
object.c             content-addressable object store (SHA-256, sharded, atomic writes)
tree.c / tree.h      tree serialization and recursive tree building
index.c / index.h    staging area
commit.c / commit.h  commit creation, parsing and history walk
test_objects.c       object store tests
test_tree.c          tree tests
test_sequence.sh     end-to-end integration test
docs/ANALYSIS.md     design answers: checkout, dirty working trees, detached HEAD, garbage collection
docs/LAB_SPEC.md     the original lab handout
```

## Further reading

- [docs/ANALYSIS.md](docs/ANALYSIS.md): how `checkout`, dirty-working-tree detection, detached HEAD recovery and garbage collection would work on top of this design.
- [Git Internals (Pro Git)](https://git-scm.com/book/en/v2/Git-Internals-Plumbing-and-Porcelain), the design this lab follows.
