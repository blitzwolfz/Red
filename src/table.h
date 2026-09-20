// Two hash tables.
//
// Table is keyed by interned ObjString pointers and backs globals, class
// method lists and instance fields. Because strings are interned, key
// comparison is a pointer compare.
//
// ValueMap is keyed by arbitrary Red values and backs the map type that
// user code sees.
#pragma once

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

 private:
  int count_ = 0;
  int capacity_ = 0;
  Entry* entries_ = nullptr;

  void adjustCapacity(int capacity);
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
  int count() const { return count_; }
  const std::vector<ValueEntry>& slots() const { return slots_; }

 private:
  int count_ = 0;
  std::vector<ValueEntry> slots_;
  void grow();
};

}  // namespace red
