// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <utils/intrusive_container_utils.h>
#include <utils/intrusive_pointer_traits.h>
#include <utils/intrusive_single_list.h>

namespace utils {
namespace newcode {

// DefaultKeyedObjectTraits defines a default implementation of traits used to
// manage objects stored in associative containers such as hash-tables and
// trees.
//
// At a minimum, a class or a struct which is to be used to define the
// traits of a keyed object must define the following public members.
//
// KeyType  : a typename which defines the type used as the key of an object.
// PtrType  : The type of the pointer to an object to be stored in the
//            associative container.  Must be one of the three pointer types
//            supported by Magenta intrusive containers : T*, unique_ptr<T> or
//            RefPtr<T>.
// GetKey   : A static method which takes a constant reference to an object (the
//            type of which is infered from PtrType) and returns a KeyType
//            instance corresponding to the key for an object.
// LessThan : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be less than key2 for
//            sorting purposes.
// EqualTo  : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be equal to key2.
//
// Rules for keys:
// ++ The key for an object must remain constant for as long as the object is
//    contained within a container.
// ++ When comparing keys, comparisons must obey basic transative and
//    commutative properties.  That is to say...
//    LessThan(A, B) and LessThan(B, C) implies LessThan(A, C)
//    EqualTo(A, B) and EqualTo(B, C) implies EqualTo(A, C)
//    EqualTo(A, B) if-and-only-if EqualTo(B, A)
//    LessThan(A, B) if-and-only-if EqualTo(B, A) or (not LessThan(B, A))
//
// DefaultKeyedObjectTraits takes its KeyType and PtrType from template
// parameters.  It requires its object type to implement a const instance
// method called GetKey which returns the object's key.  It also requires its
// KeyType to have defines < and == operators for the purpose of generating
// implementation of LessThan and EqualTo.
template <typename _KeyType, typename _PtrType>
struct DefaultKeyedObjectTraits {
    using KeyType   = _KeyType;
    using PtrType   = _PtrType;
    using PtrTraits = internal::ContainerPtrTraits<PtrType>;

    static KeyType GetKey(typename PtrTraits::ConstRefType obj)     { return obj.GetKey(); }
    static bool LessThan(const KeyType& key1, const KeyType& key2)  { return key1 < key2; }
    static bool EqualTo (const KeyType& key1, const KeyType& key2)  { return key1 == key2; }
};

// DefaultHashTraits defines a default implementation of traits used to
// define how a specific type of object is to be managed in a particular
// hash-table of pointers to that object.
//
// At a minimum, a class or a struct which is to be used to define the
// hash traits of an object/hash-table pair must define all of the traits of a
// keyed-object in addition to the following...
//
// HashType    : the typename of an unsigned integer data type which the
//               chosen hash-function will return.
// kNumBuckets : A static constexpr member which determines the number of
//               buckets stored in a hash table.
// GetHash     : A static method which take a constant reference to a KeyType
//               and returns a HashType representing the hashed value of the
//               key.  The value must be on the range from [0, kNumBuckets - 1]
//
// DefaultHashTraits takes its KeyType, PtrType, HashType and NumBuckets from
// template parameters.  It requires its object's class to implement a static
// method called GetHash whose signature an behavior match those of the GetHash
// trait method described above.
template <typename _KeyType,
          typename _PtrType,
          typename _HashType = size_t,
          _HashType _NumBuckets = 37>
struct DefaultHashTraits : public DefaultKeyedObjectTraits<_KeyType, _PtrType> {
    using KeyType   = _KeyType;
    using PtrType   = _PtrType;
    using HashType  = _HashType;
    using PtrTraits = internal::ContainerPtrTraits<PtrType>;
    using ValueType = typename PtrTraits::ValueType;

    // The number of buckets should be a nice prime such as 37, 211, 389 unless
    // The hash function is really good. Lots of cheap hash functions have
    // hidden periods for which the mod with prime above 'mostly' fixes.
    static constexpr HashType kNumBuckets = _NumBuckets;
    static HashType GetHash(const KeyType& key) {
        HashType ret = ValueType::GetHash(key);
        DEBUG_ASSERT((ret >= 0) && (ret < kNumBuckets));
        return ret;
    }
};

template <typename _HashTraits,
          typename _BucketType = SinglyLinkedList<typename _HashTraits::PtrType>>
class HashTable {
private:
    // Private fwd decls of the iterator implementation.
    template <typename IterTraits> class iterator_impl;
    class iterator_traits;
    class const_iterator_traits;

public:
    using HashTraits   = _HashTraits;
    using BucketType   = _BucketType;
    using NodeTraits   = typename BucketType::NodeTraits;
    using PtrType      = typename HashTraits::PtrType;
    using KeyType      = typename HashTraits::KeyType;
    using HashType     = typename HashTraits::HashType;
    using PtrTraits    = internal::ContainerPtrTraits<PtrType>;
    using ValueType    = typename PtrTraits::ValueType;

    // Declarations of the standard iterator types.
    using iterator       = iterator_impl<iterator_traits>;
    using const_iterator = iterator_impl<const_iterator_traits>;

    // Hash tables only support constant order erase if their underlying bucket
    // type does.
    static constexpr bool SupportsConstantOrderErase = BucketType::SupportsConstantOrderErase;
    static constexpr bool IsAssociative = true;
    static constexpr bool IsSequenced   = false;

    static constexpr HashType kNumBuckets = HashTraits::kNumBuckets;
    static_assert(kNumBuckets > 0, "Hash tables must have at least one bucket");

    constexpr HashTable() {}
    ~HashTable() { DEBUG_ASSERT(PtrTraits::IsManaged || is_empty()); }

    // Standard begin/end, cbegin/cend iterator accessors.
    iterator begin()              { return       iterator(this,       iterator::BEGIN); }
    const_iterator begin()  const { return const_iterator(this, const_iterator::BEGIN); }
    const_iterator cbegin() const { return const_iterator(this, const_iterator::BEGIN); }

    iterator end()              { return       iterator(this,       iterator::END); }
    const_iterator end()  const { return const_iterator(this, const_iterator::END); }
    const_iterator cend() const { return const_iterator(this, const_iterator::END); }

    // make_iterator : construct an iterator out of a reference to an object.
    iterator make_iterator(ValueType& obj) {
        HashType ndx = HashTraits::GetHash(HashTraits::GetKey(obj));
        return iterator(this, ndx, buckets_[ndx].make_iterator(obj));
    }

    void insert(const PtrType& ptr) { insert(PtrType(ptr)); }
    void insert(PtrType&& ptr) {
        DEBUG_ASSERT(ptr != nullptr);
        GetBucket(*ptr).push_front(utils::move(ptr));
        ++count_;
    }

    const PtrType& find(const KeyType& key) {
        BucketType& bucket = GetBucket(key);
        return bucket.find_if(
            [key](const ValueType& other) -> bool {
                return HashTraits::EqualTo(key, HashTraits::GetKey(other));
            });
    }

    PtrType erase(const KeyType& key) {
        BucketType& bucket = GetBucket(key);

        PtrType ret = internal::KeyEraseUtils<BucketType, HashTraits>::erase(bucket, key);
        if (ret != nullptr)
            --count_;

        return ret;
    }

    PtrType erase(const iterator& iter) {
        if (!iter.IsValid())
            return PtrType(nullptr);

        return direct_erase(buckets_[iter.bucket_ndx_], *iter);
    }

    PtrType erase(ValueType& obj) {
        return direct_erase(GetBucket(obj), obj);
    }

    void clear() {
        for (auto& e : buckets_)
            e.clear();
        count_ = 0;
    }

    size_t size()      const { return count_; }
    size_t size_slow() const { return size(); }
    bool   is_empty()  const { return count_ == 0; }

    // erase_if
    //
    // Find the first member of the hash table which satisfies the predicate
    // given by 'fn' and erase it from the list, returning a referenced pointer
    // to the removed element.  Return nullptr if no member satisfies the
    // predicate.
    template <typename UnaryFn>
    PtrType erase_if(UnaryFn fn) {
        if (is_empty())
            return PtrType(nullptr);

        for (HashType i = 0; i < kNumBuckets; ++i) {
            auto& bucket = buckets_[i];
            if (!bucket.is_empty()) {
                PtrType ret = bucket.erase_if(fn);
                if (ret != nullptr) {
                    --count_;
                    return ret;
                }
            }
        }

        return PtrType(nullptr);
    }

    // find_if
    //
    // Find the first member of the hash table which satisfies the predicate
    // given by 'fn' and return a const& to the PtrType in the hash table which
    // refers to it.  Return nullptr if no member satisfies the predicate.
    template <typename UnaryFn>
    const PtrType& find_if(UnaryFn fn) {
        static PtrType null_ptr;
        if (is_empty())
            return null_ptr;

        for (HashType i = 0; i < kNumBuckets; ++i) {
            auto& bucket = buckets_[i];
            if (!bucket.is_empty()) {
                const PtrType& ret = bucket.find_if(fn);
                if (ret != nullptr)
                    return ret;
            }
        }

        return null_ptr;
    }

private:
    // The traits of a non-const iterator
    struct iterator_traits {
        using RefType    = typename PtrTraits::RefType;
        using RawPtrType = typename PtrTraits::RawPtrType;
        using IterType   = typename BucketType::iterator;
        using HTPtrType  = HashTable<HashTraits, BucketType>*;

        static IterType BucketBegin(BucketType& bucket) { return bucket.begin(); }
        static IterType BucketEnd  (BucketType& bucket) { return bucket.end(); }
    };

    // The traits of a const iterator
    struct const_iterator_traits {
        using RefType    = typename PtrTraits::ConstRefType;
        using RawPtrType = typename PtrTraits::ConstRawPtrType;
        using IterType   = typename BucketType::const_iterator;
        using HTPtrType  = const HashTable<HashTraits, BucketType>*;

        static IterType BucketBegin(const BucketType& bucket) { return bucket.cbegin(); }
        static IterType BucketEnd  (const BucketType& bucket) { return bucket.cend(); }
    };

    // The shared implementation of the iterator
    template <class IterTraits>
    class iterator_impl {
    public:
        iterator_impl() { }
        iterator_impl(const iterator_impl& other) {
            hash_table_ = other.hash_table_;
            bucket_ndx_ = other.bucket_ndx_;
            iter_       = other.iter_;
        }

        iterator_impl& operator=(const iterator_impl& other) {
            hash_table_ = other.hash_table_;
            bucket_ndx_ = other.bucket_ndx_;
            iter_       = other.iter_;
            return *this;
        }

        bool IsValid() const { return iter_.IsValid(); }
        bool operator==(const iterator_impl& other) const { return iter_ == other.iter_; }
        bool operator!=(const iterator_impl& other) const { return iter_ != other.iter_; }

        // Prefix
        iterator_impl& operator++() {
            if (!IsValid()) return *this;
            DEBUG_ASSERT(hash_table_);

            // Bump the bucket iterator and go looking for a new bucket if the
            // iterator has become invalid.
            ++iter_;
            advance_if_invalid_iter();

            return *this;
        }

        iterator_impl& operator--() {
            // If we have never been bound to a HashTable instance, the we had
            // better be invalid.
            if (!hash_table_) {
                DEBUG_ASSERT(!IsValid());
                return *this;
            }

            // Back up the bucket iterator.  If it is still valid, then we are done.
            --iter_;
            if (iter_.IsValid())
                return *this;

            // If the iterator is invalid after backing up, check previous
            // buckets to see if they contain any nodes.
            while (bucket_ndx_) {
                --bucket_ndx_;
                auto& bucket = hash_table_->buckets_[bucket_ndx_];
                if (!bucket.is_empty()) {
                    iter_ = --IterTraits::BucketEnd(bucket);
                    DEBUG_ASSERT(iter_.IsValid());
                    return *this;
                }
            }

            // Looks like we have backed up past the beginning.  Update the
            // bookkeeping to point at the end of the last bucket.
            bucket_ndx_ = kNumBuckets - 1;
            iter_ = IterTraits::BucketEnd(hash_table_->buckets_[bucket_ndx_]);

            return *this;
        }

        // Postfix
        iterator_impl operator++(int) {
            iterator_impl ret(*this);
            ++(*this);
            return ret;
        }

        iterator_impl operator--(int) {
            iterator_impl ret(*this);
            --(*this);
            return ret;
        }

        typename PtrTraits::PtrType CopyPointer()          { return iter_.CopyPointer(); }
        typename IterTraits::RefType operator*()     const { return iter_.operator*(); }
        typename IterTraits::RawPtrType operator->() const { return iter_.operator->(); }

    private:
        friend class HashTable<HashTraits, BucketType>;
        using HTPtrType = typename IterTraits::HTPtrType;
        using IterType  = typename IterTraits::IterType;

        enum BeginTag { BEGIN };
        enum EndTag { END };

        iterator_impl(HTPtrType hash_table, BeginTag)
            : hash_table_(hash_table),
              bucket_ndx_(0),
              iter_(IterTraits::BucketBegin(hash_table->buckets_[0])) {
            advance_if_invalid_iter();
        }

        iterator_impl(HTPtrType hash_table, EndTag)
            : hash_table_(hash_table),
              bucket_ndx_(kNumBuckets - 1),
              iter_(IterTraits::BucketEnd(hash_table->buckets_[kNumBuckets - 1])) { }

        iterator_impl(HTPtrType hash_table, size_t bucket_ndx, IterType iter)
            : hash_table_(hash_table),
              bucket_ndx_(bucket_ndx),
              iter_(iter) { }

        void advance_if_invalid_iter() {
            // If the iterator has run off the end of it's current bucket, then
            // check to see if there are nodes in any of the remaining buckets.
            if (!iter_.IsValid()) {
                while (bucket_ndx_ < (kNumBuckets - 1)) {
                    ++bucket_ndx_;
                    auto& bucket = hash_table_->buckets_[bucket_ndx_];

                    if (!bucket.is_empty()) {
                        iter_ = IterTraits::BucketBegin(bucket);
                        DEBUG_ASSERT(iter_.IsValid());
                        break;
                    } else if (bucket_ndx_ == (kNumBuckets - 1)) {
                        iter_ = IterTraits::BucketEnd(bucket);
                    }
                }
            }
        }

        HTPtrType hash_table_ = nullptr;
        size_t bucket_ndx_ = 0;
        IterType iter_;
    };

    PtrType direct_erase(BucketType& bucket, ValueType& obj) {
        PtrType ret = internal::DirectEraseUtils<BucketType>::erase(bucket, obj);

        if (ret != nullptr)
            --count_;

        return ret;
    }

    // Iterators need to access our bucket array in order to iterate.
    friend iterator;
    friend const_iterator;

    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    BucketType& GetBucket(const KeyType& key) { return buckets_[HashTraits::GetHash(key)]; }
    BucketType& GetBucket(const ValueType& obj) { return GetBucket(HashTraits::GetKey(obj)); }

    size_t count_ = 0UL;
    BucketType buckets_[kNumBuckets];
};

template <typename KeyType, typename PtrType>
class DefaultHashTable : public HashTable<DefaultHashTraits<KeyType, PtrType>> { };

}  // namespace newcode
}  // namespace utils