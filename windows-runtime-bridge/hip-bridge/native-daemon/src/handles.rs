//! Maps opaque u64 handles (safe to hand across the network to hip-stub)
//! to real device pointers that only ever live in this process.
//!
//! Generic over the stored value so the bookkeeping logic - the part
//! worth testing - can be exercised with plain integers in tests,
//! without needing a real HIP device or unsafe raw pointers.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

pub struct HandleTable<T> {
    next_id: AtomicU64,
    entries: Mutex<HashMap<u64, T>>,
}

impl<T> Default for HandleTable<T> {
    fn default() -> Self {
        HandleTable {
            next_id: AtomicU64::new(1), // 0 reserved as "never a valid handle"
            entries: Mutex::new(HashMap::new()),
        }
    }
}

impl<T> HandleTable<T> {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn insert(&self, value: T) -> u64 {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed);
        self.entries.lock().unwrap().insert(id, value);
        id
    }

    pub fn remove(&self, id: u64) -> Option<T> {
        self.entries.lock().unwrap().remove(&id)
    }

    pub fn len(&self) -> usize {
        self.entries.lock().unwrap().len()
    }
}

impl<T: Copy> HandleTable<T> {
    pub fn get(&self, id: u64) -> Option<T> {
        self.entries.lock().unwrap().get(&id).copied()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_then_get_returns_the_value() {
        let table: HandleTable<usize> = HandleTable::new();
        let id = table.insert(42);
        assert_eq!(table.get(id), Some(42));
    }

    #[test]
    fn handles_are_never_reused_within_a_table() {
        let table: HandleTable<usize> = HandleTable::new();
        let a = table.insert(1);
        let b = table.insert(2);
        assert_ne!(a, b);
    }

    #[test]
    fn zero_is_never_issued_as_a_handle() {
        let table: HandleTable<usize> = HandleTable::new();
        for _ in 0..10 {
            assert_ne!(table.insert(0), 0);
        }
    }

    #[test]
    fn remove_returns_the_value_and_forgets_it() {
        let table: HandleTable<usize> = HandleTable::new();
        let id = table.insert(7);
        assert_eq!(table.remove(id), Some(7));
        assert_eq!(table.get(id), None);
        assert_eq!(table.remove(id), None);
    }

    #[test]
    fn get_on_unknown_handle_is_none() {
        let table: HandleTable<usize> = HandleTable::new();
        assert_eq!(table.get(999), None);
    }

    #[test]
    fn len_tracks_live_entries() {
        let table: HandleTable<usize> = HandleTable::new();
        assert_eq!(table.len(), 0);
        let a = table.insert(1);
        let _b = table.insert(2);
        assert_eq!(table.len(), 2);
        table.remove(a);
        assert_eq!(table.len(), 1);
    }
}
