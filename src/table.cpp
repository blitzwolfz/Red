#include "table.h"

#include <cassert>
#include <cstring>
#include <mutex>

#include "object.h"

namespace red {

namespace {
// Grow at 75% load. Above that, linear probing starts to cluster badly.
constexpr double kMaxLoad = 0.75;
}  // namespace

Table::~Table() { delete[] entries_; }

// A tombstone is an entry with a null key and a true value. It keeps probe
// sequences intact after a removal.
//
// Keys are compared by pointer, which is only correct because every key
// that reaches this table is interned: identifiers and method names come
// from a chunk's constant pool, and the compiler and the .redc reader
// both intern those whatever their length. Table::set() checks that in a
// debug build. Maps written by a program use ValueMap below, which
// compares contents.
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash & (uint32_t)(capacity - 1);
  Entry* tombstone = nullptr;
  for (;;) {
    Entry* entry = &entries[index];
    if (entry->key == nullptr) {
      if (isNil(entry->value)) return tombstone != nullptr ? tombstone : entry;
      if (tombstone == nullptr) tombstone = entry;
    } else if (entry->key == key) {
      return entry;
    }
    index = (index + 1) & (uint32_t)(capacity - 1);
  }
}

// Arrays that resizes replaced, kept until the collector stops the
// world. A reader may still be walking one, and it has no way to say so.
namespace {
std::mutex g_retiredMutex;
std::vector<Entry*> g_retired;
}  // namespace

void Table::releaseRetiredArrays() {
  std::lock_guard<std::mutex> guard(g_retiredMutex);
  for (Entry* entries : g_retired) delete[] entries;
  g_retired.clear();
}

void Table::adjustCapacity(int capacity) {
  Entry* entries = new Entry[capacity];
  for (int i = 0; i < capacity; i++) {
    entries[i].key = nullptr;
    entries[i].value = nilValue();
  }

  // Rehashing drops tombstones, so the count is rebuilt from scratch.
  count_ = 0;
  for (int i = 0; i < capacity_; i++) {
    Entry* entry = &entries_[i];
    if (entry->key == nullptr) continue;
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    count_++;
  }

  if (runningInParallel() && entries_ != nullptr) {
    // Someone may be part way through a lookup in the old array. It is
    // kept rather than freed, and goes at the next collection.
    std::lock_guard<std::mutex> guard(g_retiredMutex);
    g_retired.push_back(entries_);
  } else {
    delete[] entries_;
  }
  entries_ = entries;
  capacity_ = capacity;
}

bool Table::get(ObjString* key, Value* out) const {
  if (!runningInParallel()) {
    if (count_ == 0) return false;
    Entry* entry = findEntry(entries_, capacity_, key);
    if (entry->key == nullptr) return false;
    *out = entry->value;
    return true;
  }

  for (;;) {
    uint32_t before = version_.load(std::memory_order_acquire);
    // Odd means a writer is inside the table right now.
    if ((before & 1) != 0) continue;

    Entry* entries = entries_;
    int capacity = capacity_;
    // The array and its size have to come from the same moment, or the
    // probe could run off the end of one with the size of the other.
    if (version_.load(std::memory_order_acquire) != before) continue;
    if (capacity == 0) return false;

    Entry* entry = findEntry(entries, capacity, key);
    bool found = entry->key != nullptr;
    Value value = entry->value;
    // Only now is what was read known to be a value that was really
    // there. Anything read from a table a writer was moving is thrown
    // away and the probe runs again.
    std::atomic_thread_fence(std::memory_order_acquire);
    if (version_.load(std::memory_order_relaxed) != before) continue;

    if (!found) return false;
    *out = value;
    return true;
  }
}

bool Table::set(ObjString* key, Value value) {
  assert(key != nullptr);
  Writing writing(*this);
  if ((double)count_ + 1 > (double)capacity_ * kMaxLoad) {
    adjustCapacity(capacity_ < 8 ? 8 : capacity_ * 2);
  }
  Entry* entry = findEntry(entries_, capacity_, key);
  bool isNew = entry->key == nullptr;
  // Reusing a tombstone does not change the count, because the tombstone
  // was already counted when it was created.
  if (isNew && isNil(entry->value)) count_++;
  entry->key = key;
  entry->value = value;
  return isNew;
}

bool Table::remove(ObjString* key) {
  if (count_ == 0) return false;
  Writing writing(*this);
  Entry* entry = findEntry(entries_, capacity_, key);
  if (entry->key == nullptr) return false;
  entry->key = nullptr;
  entry->value = boolValue(true);
  return true;
}

void Table::addAll(const Table& from) {
  for (int i = 0; i < from.capacity_; i++) {
    Entry* entry = &from.entries_[i];
    if (entry->key != nullptr) set(entry->key, entry->value);
  }
}

ObjString* Table::findString(const char* chars, size_t length,
                             uint32_t hash) const {
  // The interner is the one table where a lookup and the insert that
  // follows it have to be one step, or two threads would each make a
  // string for the same text and the pointer comparison every other
  // table relies on would stop meaning anything. Runtime holds a lock
  // across both, so this needs no seqlock of its own.
  if (count_ == 0) return nullptr;
  uint32_t index = hash & (uint32_t)(capacity_ - 1);
  for (;;) {
    Entry* entry = &entries_[index];
    if (entry->key == nullptr) {
      if (isNil(entry->value)) return nullptr;
    } else if (entry->key->length == length && entry->key->hash == hash &&
               std::memcmp(entry->key->chars, chars, length) == 0) {
      return entry->key;
    }
    index = (index + 1) & (uint32_t)(capacity_ - 1);
  }
}

void Table::removeUnmarked() {
  for (int i = 0; i < capacity_; i++) {
    Entry* entry = &entries_[i];
    if (entry->key != nullptr && !entry->key->obj.isMarked) {
      remove(entry->key);
    }
  }
}

// ---------------------------------------------------------------------

void ValueMap::grow() {
  size_t newCapacity = slots_.empty() ? 8 : slots_.size() * 2;
  std::vector<ValueEntry> old;
  old.swap(slots_);
  slots_.assign(newCapacity, ValueEntry{nilValue(), nilValue(), false, false});
  count_ = 0;
  for (ValueEntry& e : old) {
    if (!e.used || e.tombstone) continue;
    set(e.key, e.value);
  }
}

// Returns the slot a key belongs in, or nullptr when the table is empty.
static ValueEntry* findValueSlot(std::vector<ValueEntry>& slots, Value key) {
  if (slots.empty()) return nullptr;
  size_t mask = slots.size() - 1;
  size_t index = hashValue(key) & mask;
  ValueEntry* tombstone = nullptr;
  for (;;) {
    ValueEntry* slot = &slots[index];
    if (!slot->used) {
      if (!slot->tombstone) return tombstone != nullptr ? tombstone : slot;
      if (tombstone == nullptr) tombstone = slot;
    } else if (valuesEqual(slot->key, key)) {
      return slot;
    }
    index = (index + 1) & mask;
  }
}

bool ValueMap::get(Value key, Value* out) const {
  if (count_ == 0) return false;
  ValueEntry* slot =
      findValueSlot(const_cast<std::vector<ValueEntry>&>(slots_), key);
  if (slot == nullptr || !slot->used) return false;
  *out = slot->value;
  return true;
}

bool ValueMap::set(Value key, Value value) {
  if (slots_.empty() || (double)(count_ + 1) > (double)slots_.size() * kMaxLoad) {
    grow();
  }
  ValueEntry* slot = findValueSlot(slots_, key);
  bool isNew = !slot->used;
  if (isNew) count_++;
  slot->key = key;
  slot->value = value;
  slot->used = true;
  slot->tombstone = false;
  return isNew;
}

bool ValueMap::remove(Value key) {
  if (count_ == 0) return false;
  ValueEntry* slot = findValueSlot(slots_, key);
  if (slot == nullptr || !slot->used) return false;
  slot->used = false;
  slot->tombstone = true;
  slot->key = nilValue();
  slot->value = nilValue();
  count_--;
  return true;
}

}  // namespace red
