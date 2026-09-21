// Two hash tables.
//
// Table is keyed by interned ObjString pointers and backs globals, class
// method lists and instance fields. Because strings are interned, key
// comparison is a pointer compare.
//
// ValueMap is keyed by arbitrary Red values and backs the map type that
// user code sees.
//
// Both are read far more often than they are written: a global lookup
// and a method call each go through one on every occurrence. So once
// more than one thread is running, a lookup takes no lock at all. It
// reads a counter either side of the probe, and retries if a writer
// moved it in between. Writers take a small lock and move the counter,
// and a resize keeps the old array rather than freeing it, because a
// reader may still be walking it. The kept arrays go when the collector
// next stops the world, which is the one moment no thread can be inside
// a lookup.
#pragma once

#include <atomic>

#include "common.h"
#include "value.h"

namespace red {

struct ObjString;

struct Entry {
  ObjString* key;
  Value value;
};

class Table {
 public:
  Table() = default;
  ~Table();
  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;

  bool get(ObjString* key, Value* out) const;
  // Returns true when the key was not already present.
  bool set(ObjString* key, Value value);
  bool remove(ObjString* key);
  void addAll(const Table& from);

  // Used by the string interner. Compares contents, not pointers, because
  // the caller does not have an interned string yet.
  ObjString* findString(const char* chars, size_t length, uint32_t hash) const;

  // Drops entries whose key was not marked in this cycle. The interner is
  // a weak table: it must not keep a string alive by itself.
  void removeUnmarked();

  Entry* entries() const { return entries_; }
  int capacity() const { return capacity_; }
  int count() const { return count_; }

  // Frees the arrays that resizes replaced. Only safe with the world
  // stopped, which is where the collector calls it from.
  static void releaseRetiredArrays();

 private:
  int count_ = 0;
  int capacity_ = 0;
  Entry* entries_ = nullptr;
  // Even when the table is settled, odd while a writer is inside it. A
  // reader that sees the same even number either side of its probe saw a
  // table nobody was changing.
  mutable std::atomic<uint32_t> version_{0};
  // Held by writers, so that two of them do not interleave. A plain flag
  // rather than a mutex: it is held for the length of a probe, and every
  // instance carries one of these tables.
  mutable std::atomic_flag writing_ = ATOMIC_FLAG_INIT;

  void adjustCapacity(int capacity);

  // Marks the table as being written for as long as the guard lives.
  // Does nothing at all until a second thread exists.
  class Writing {
   public:
    explicit Writing(const Table& table) : table_(table) {
      if (!runningInParallel()) return;
      held_ = true;
      while (table_.writing_.test_and_set(std::memory_order_acquire)) {
      }
      table_.version_.fetch_add(1, std::memory_order_release);
    }
    ~Writing() {
      if (!held_) return;
      table_.version_.fetch_add(1, std::memory_order_release);
      table_.writing_.clear(std::memory_order_release);
    }
    Writing(const Writing&) = delete;
    Writing& operator=(const Writing&) = delete;

   private:
    const Table& table_;
    bool held_ = false;
  };
};

struct ValueEntry {
  Value key;
  Value value;
  bool used;
  bool tombstone;
};

class ValueMap {
 public:
  bool get(Value key, Value* out) const;
  bool set(Value key, Value value);
  bool remove(Value key);
  // Drops every entry and the storage with them. set() grows again from
  // nothing, the same way it did when the table was new.
  void clear() {
    count_ = 0;
    slots_.clear();
  }
  int count() const { return count_; }
  const std::vector<ValueEntry>& slots() const { return slots_; }

 private:
  int count_ = 0;
  std::vector<ValueEntry> slots_;
  void grow();
};

}  // namespace red
