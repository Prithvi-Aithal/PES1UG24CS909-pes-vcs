# Design analysis: branching, checkout and garbage collection

Written answers to the analysis questions of the PES-VCS lab (phases 5 and 6). These features are designed here, not implemented in the code.

## Phase 5: Branching and Checkout (Analysis)

### Q5.1 — How would you implement `pes checkout <branch>`?

To implement `pes checkout <branch>`, the following must happen:

**Changes to `.pes/`:**
- Update `.pes/HEAD` to contain `ref: refs/heads/<branch>` pointing to the new branch.
- If the branch doesn't exist yet (`pes checkout -b <branch>`), create the file `.pes/refs/heads/<branch>` containing the current commit hash.

**Changes to the working directory:**
- Read the target branch's commit hash from `.pes/refs/heads/<branch>`.
- Load that commit object and get its tree hash.
- Recursively walk the tree and for every blob entry, write the file contents to disk at the correct path.
- Delete any tracked files from the old branch that don't exist in the new branch's tree.

**What makes it complex:**
1. **Dirty working directory** — if the user has modified files, those changes could be silently overwritten. You must detect and refuse if there are conflicts.
2. **Recursive tree traversal** — you must recursively restore nested subdirectories, creating them if they don't exist.
3. **Deletions** — files tracked in the old HEAD but absent from the new branch tree must be deleted from disk.
4. **Atomicity** — if checkout fails halfway, the working directory is in a broken state. Real Git uses a staged approach to minimize this window.

---

### Q5.2 — How would you detect a dirty working directory conflict?

When switching branches, for each file tracked in the current index:

1. **Stat the file on disk** — get its current `mtime` and `size`.
2. **Compare with index metadata** — if `mtime` or `size` differs from what's stored in the index entry, the file has been modified since it was last staged.
3. **Compare index hash with target branch** — read the target branch's commit tree and check if the file's blob hash differs from the index hash.

If a file is **both modified on disk AND different between branches**, checkout must refuse with an error like `error: your local changes would be overwritten by checkout`. If only the branches differ but the working file is clean (matches index), checkout can safely update the file. This is exactly how Git's fast metadata check works — it avoids re-hashing every file by using mtime+size as a proxy for "unchanged."

---

### Q5.3 — What happens in detached HEAD state and how do you recover?

**Detached HEAD** means `.pes/HEAD` contains a raw commit hash like `a1b2c3...` instead of `ref: refs/heads/main`. This happens when you checkout a specific commit rather than a branch name.

If you make commits in this state:
- New commits are created and chained correctly with parent pointers.
- But no branch ref is updated — the commits are only reachable via HEAD directly.
- As soon as you checkout another branch, HEAD moves away and those commits become **unreachable** — no branch points to them.

**Recovery:**
- If you haven't switched away yet, run `pes branch recovery-branch` to create a new branch at the current HEAD hash — this saves the commit chain.
- If you already switched away, you need to find the lost commit hash from memory or a log (real Git keeps `.git/logs/HEAD` — a reflog — for exactly this reason). Then create a branch pointing to it: `pes branch recovery-branch <hash>`.

---

## Phase 6: Garbage Collection (Analysis)

### Q6.1 — Algorithm to find and delete unreachable objects

**Algorithm (Mark and Sweep):**

1. **Mark phase** — find all reachable objects:
   - Start from every branch ref in `.pes/refs/heads/` and HEAD.
   - For each commit hash, add it to a `reachable` hash set.
   - Read the commit object, add its tree hash to the set.
   - Recursively walk each tree: add every sub-tree and blob hash to the set.
   - Follow each commit's parent pointer and repeat until no parent exists.

2. **Sweep phase** — delete unreachable objects:
   - Walk every file under `.pes/objects/` using `find` or `opendir/readdir`.
   - Reconstruct each object's hash from its path (first 2 chars + rest of filename).
   - If the hash is NOT in the `reachable` set, delete the file.

**Data structure:** A hash set (e.g., a hash table or sorted array of `ObjectID`) for O(1) or O(log n) lookup during the sweep.

**Estimate for 100,000 commits, 50 branches:**
- Each commit has 1 tree + average ~20 blobs + sub-trees → ~25 objects per commit.
- 100,000 commits × 25 = ~2,500,000 objects to visit in the mark phase.
- Plus 50 branch starting points (negligible).
- Total: approximately **2.5 million object reads** in a full GC pass.

---

### Q6.2 — Race condition between GC and concurrent commit

**The race condition:**

1. A `commit` operation calls `tree_from_index()` and writes several blob objects to the store. At this point the blobs exist but no commit or tree object points to them yet — they are temporarily unreachable.
2. Concurrently, GC runs its mark phase, scans all refs, and does NOT see these new blobs (because the commit hasn't been written yet).
3. GC's sweep phase deletes the blobs as unreachable.
4. The commit operation now tries to write the tree and commit objects referencing those blobs — but the blobs are gone. The repository is now corrupt.

**How Git avoids this:**
- Git uses a **grace period** — objects newer than a configurable threshold (default 2 weeks) are never deleted by GC, even if unreachable. This gives any in-progress operations time to complete and reference the new objects.
- Git also writes a **lock file** (`.git/gc.pid`) so only one GC runs at a time.
- Loose objects are only packed/deleted after they've existed long enough that no concurrent operation could still be referencing them without having written a ref.

