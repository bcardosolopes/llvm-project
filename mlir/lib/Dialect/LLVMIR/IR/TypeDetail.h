//===- TypeDetail.h - Details of MLIR LLVM dialect types --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains implementation details, such as storage structures, of
// MLIR LLVM dialect types.
//
//===----------------------------------------------------------------------===//

#ifndef DIALECT_LLVMIR_IR_TYPEDETAIL_H
#define DIALECT_LLVMIR_IR_TYPEDETAIL_H

#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/IR/Types.h"

#include "llvm/ADT/Bitfields.h"
#include "llvm/ADT/PointerIntPair.h"

#include <atomic>

namespace mlir {
namespace LLVM {
namespace detail {

//===----------------------------------------------------------------------===//
// LLVMStructTypeStorage.
//===----------------------------------------------------------------------===//

/// Type storage for LLVM structure types.
///
/// Structures are uniqued using:
/// - a bit indicating whether a struct is literal or identified;
/// - for identified structs, in addition to the bit:
///   - a string identifier;
///   - a bit indicating whether the struct is frozen;
///   - for frozen structs only: the opaque bit, the packed bit and the list of
///     contained types;
/// - for literal structs, in addition to the bit:
///   - a list of contained types;
///   - a bit indicating whether the literal struct is packed.
///
/// Identified structures only have a mutable component consisting of:
///   - a list of contained types;
///   - a bit indicating whether the identified struct is packed;
///   - a bit indicating whether the identified struct is intentionally opaque;
///   - a bit indicating whether the identified struct has been initialized.
/// Uninitialized structs are considered opaque by the user, and can be mutated.
/// Initialized and still opaque structs cannot be mutated.
///
/// Frozen identified structs are created fully initialized and never change.
/// Two frozen structs with the same name but a different body or packedness
/// are different types. A frozen struct is never equal to a non-frozen struct
/// with the same name.
///
/// The struct storage consists of:
///   - immutable part:
///     - a pointer to the first element of the key (character for identified
///       structs, type for literal structs);
///     - the number of elements in the key packed together with bits indicating
///       whether a type is literal or identified, and the packedness bit for
///       literal structs only;
///   - mutable part:
///     - a pointer to the first contained type for identified structs only;
///     - the number of contained types packed together with bits of the mutable
///       component, for identified structs only.
struct LLVMStructTypeStorage : public TypeStorage {
public:
  /// Construction/uniquing key class for LLVM dialect structure storage. Note
  /// that this is a transient helper data structure that is NOT stored.
  /// Therefore, it intentionally avoids bit manipulation and type erasure in
  /// pointers to make manipulation more straightforward. Not all elements of
  /// the key participate in uniquing, but all elements participate in
  /// construction.
  class Key {
  public:
    struct FrozenTag {};

    /// Constructs a key for an identified struct.
    Key(StringRef name, bool opaque, ArrayRef<Type> types = {})
        : types(types), name(name), identified(true), packed(false),
          opaque(opaque), frozen(false) {}
    /// Constructs a key for a frozen identified struct.
    Key(FrozenTag, StringRef name, bool opaque, ArrayRef<Type> types,
        bool packed)
        : types(types), name(name), identified(true), packed(packed),
          opaque(opaque), frozen(true) {}
    /// Constructs a key for a literal struct.
    Key(ArrayRef<Type> types, bool packed)
        : types(types), identified(false), packed(packed), opaque(false),
          frozen(false) {}

    /// Checks a specific property of the struct.
    bool isIdentified() const { return identified; }
    bool isFrozen() const { return frozen; }
    bool isPacked() const {
      assert((!isIdentified() || isFrozen()) &&
             "'packed' bit is not part of the key for non-frozen identified "
             "structs");
      return packed;
    }
    bool isOpaque() const {
      assert(isIdentified() &&
             "'opaque' bit is meaningless on literal structs");
      return opaque;
    }

    /// Returns the identifier of a key for identified structs.
    StringRef getIdentifier() const {
      assert(isIdentified() &&
             "non-identified struct key cannot have an identifier");
      return name;
    }

    /// Returns the list of type contained in the key of a literal struct.
    ArrayRef<Type> getTypeList() const {
      assert(!isIdentified() &&
             "identified struct key cannot have a type list");
      return types;
    }

    /// Returns the list of type contained in an identified struct.
    ArrayRef<Type> getIdentifiedStructBody() const {
      assert(isIdentified() &&
             "requested struct body on a non-identified struct");
      return types;
    }

    /// Returns the hash value of the key. This combines various flags into a
    /// single value: the identified flag sets the first bit, the packedness
    /// flag sets the second bit, and the frozen flag sets the third bit. For
    /// non-frozen identified structs, the opacity bit is only used for
    /// construction and does not participate in uniquing.
    llvm::hash_code hashValue() const {
      constexpr static unsigned kIdentifiedHashFlag = 1;
      constexpr static unsigned kPackedHashFlag = 2;
      constexpr static unsigned kFrozenHashFlag = 4;

      unsigned flags = 0;
      if (isIdentified()) {
        flags |= kIdentifiedHashFlag;
        if (!isFrozen())
          return llvm::hash_combine(flags, getIdentifier());
        flags |= kFrozenHashFlag;
        if (isPacked())
          flags |= kPackedHashFlag;
        return llvm::hash_combine(flags, getIdentifier(), isOpaque(),
                                  getIdentifiedStructBody());
      }
      if (isPacked())
        flags |= kPackedHashFlag;
      return llvm::hash_combine(flags, getTypeList());
    }

    /// Compares two keys.
    bool operator==(const Key &other) const {
      if (isIdentified()) {
        if (!other.isIdentified() || other.isFrozen() != isFrozen() ||
            other.getIdentifier() != getIdentifier())
          return false;
        return !isFrozen() ||
               (other.isOpaque() == isOpaque() &&
                other.isPacked() == isPacked() &&
                other.getIdentifiedStructBody() == getIdentifiedStructBody());
      }

      return !other.isIdentified() && other.isPacked() == isPacked() &&
             other.getTypeList() == getTypeList();
    }

    /// Copies dynamically-sized components of the key into the given allocator.
    Key copyIntoAllocator(TypeStorageAllocator &allocator) const {
      if (isIdentified() && isFrozen())
        return Key(FrozenTag{}, allocator.copyInto(name), opaque,
                   allocator.copyInto(types), packed);
      if (isIdentified())
        return Key(allocator.copyInto(name), opaque);
      return Key(allocator.copyInto(types), packed);
    }

  private:
    ArrayRef<Type> types;
    StringRef name;
    bool identified;
    bool packed;
    bool opaque;
    bool frozen;
  };
  using KeyTy = Key;

  /// Returns the string identifier of an identified struct.
  StringRef getIdentifier() const {
    assert(isIdentified() && "requested identifier on a non-identified struct");
    return StringRef(static_cast<const char *>(keyPtr), keySize());
  }

  /// Returns the list of types (partially) identifying a literal struct.
  ArrayRef<Type> getTypeList() const {
    // If this triggers, use getIdentifiedStructBody() instead.
    assert(!isIdentified() && "requested typelist on an identified struct");
    return ArrayRef<Type>(static_cast<const Type *>(keyPtr), keySize());
  }

  /// Returns the list of types contained in an identified struct.
  ArrayRef<Type> getIdentifiedStructBody() const {
    // If this triggers, use getTypeList() instead.
    assert(isIdentified() &&
           "requested struct body on a non-identified struct");
    unsigned word = loadMutableWord();
    if (!llvm::Bitfield::get<MutableFlagInitialized>(word))
      return {};
    return ArrayRef<Type>(identifiedBodyArray,
                          llvm::Bitfield::get<MutableSize>(word));
  }

  /// Checks whether the struct is identified.
  bool isIdentified() const {
    return llvm::Bitfield::get<KeyFlagIdentified>(keySizeAndFlags);
  }

  /// Checks whether the struct is a frozen identified struct.
  bool isFrozen() const {
    return llvm::Bitfield::get<KeyFlagFrozen>(keySizeAndFlags);
  }

  /// Checks whether the struct is packed (both literal and identified structs).
  bool isPacked() const {
    return isIdentified()
               ? llvm::Bitfield::get<MutableFlagPacked>(loadMutableWord())
               : llvm::Bitfield::get<KeyFlagPacked>(keySizeAndFlags);
  }

  /// Checks whether a struct is marked as intentionally opaque (an
  /// uninitialized struct is also considered opaque by the user, call
  /// isInitialized to check that).
  bool isOpaque() const {
    return llvm::Bitfield::get<MutableFlagOpaque>(loadMutableWord());
  }

  /// Checks whether an identified struct has been explicitly initialized either
  /// by setting its body or by marking it as intentionally opaque.
  bool isInitialized() const {
    return llvm::Bitfield::get<MutableFlagInitialized>(loadMutableWord());
  }

  /// Constructs the storage from the given key. This sets up the uniquing key
  /// components and optionally the mutable component if they construction key
  /// has the relevant information. In the latter case, the struct is considered
  /// as initialized and can no longer be mutated.
  LLVMStructTypeStorage(const KeyTy &key) {
    if (!key.isIdentified()) {
      ArrayRef<Type> types = key.getTypeList();
      keyPtr = static_cast<const void *>(types.data());
      setKeySize(types.size());
      llvm::Bitfield::set<KeyFlagPacked>(keySizeAndFlags, key.isPacked());
      return;
    }

    StringRef name = key.getIdentifier();
    keyPtr = static_cast<const void *>(name.data());
    setKeySize(name.size());
    llvm::Bitfield::set<KeyFlagIdentified>(keySizeAndFlags, true);

    unsigned word = 0;
    if (key.isFrozen()) {
      llvm::Bitfield::set<KeyFlagFrozen>(keySizeAndFlags, true);
      ArrayRef<Type> body = key.getIdentifiedStructBody();
      identifiedBodyArray = body.data();
      llvm::Bitfield::set<MutableSize>(word, body.size());
      llvm::Bitfield::set<MutableFlagPacked>(word, key.isPacked());
      llvm::Bitfield::set<MutableFlagOpaque>(word, key.isOpaque());
      llvm::Bitfield::set<MutableFlagInitialized>(word, true);
    } else {
      // If the struct is being constructed directly as opaque, mark it as
      // initialized.
      llvm::Bitfield::set<MutableFlagInitialized>(word, key.isOpaque());
      llvm::Bitfield::set<MutableFlagOpaque>(word, key.isOpaque());
    }
    identifiedBodySizeAndFlags.store(word, std::memory_order_relaxed);
  }

  /// Hook into the type uniquing infrastructure. Lookups may run on other
  /// threads while `mutate` runs without a common lock, so this must only read
  /// fields that are fixed at construction. For non-frozen identified structs
  /// that is only the name.
  bool operator==(const KeyTy &other) const {
    if (!isIdentified())
      return !other.isIdentified() && other.isPacked() == isPacked() &&
             other.getTypeList() == getTypeList();
    if (!other.isIdentified() || other.isFrozen() != isFrozen() ||
        other.getIdentifier() != getIdentifier())
      return false;
    return !isFrozen() ||
           (other.isOpaque() == isOpaque() && other.isPacked() == isPacked() &&
            other.getIdentifiedStructBody() == getIdentifiedStructBody());
  }
  static llvm::hash_code hashKey(const KeyTy &key) { return key.hashValue(); }
  static LLVMStructTypeStorage *construct(TypeStorageAllocator &allocator,
                                          const KeyTy &key) {
    return new (allocator.allocate<LLVMStructTypeStorage>())
        LLVMStructTypeStorage(key.copyIntoAllocator(allocator));
  }

  /// Sets the body of an identified struct. If the struct is already
  /// initialized, succeeds only if the body is equal to the current body. Fails
  /// if the struct is marked as intentionally opaque. The struct will be marked
  /// as initialized as a result of this operation and can no longer be changed.
  LogicalResult mutate(TypeStorageAllocator &allocator, ArrayRef<Type> body,
                       bool packed) {
    if (!isIdentified())
      return failure();
    if (isInitialized())
      return success(!isOpaque() && body == getIdentifiedStructBody() &&
                     packed == isPacked());

    // Readers take no lock. Write the body before the release store that sets
    // the initialized bit, so a reader that sees the bit also sees the body.
    ArrayRef<Type> typesInAllocator = allocator.copyInto(body);
    identifiedBodyArray = typesInAllocator.data();
    unsigned word = loadMutableWord();
    llvm::Bitfield::set<MutableSize>(word, typesInAllocator.size());
    llvm::Bitfield::set<MutableFlagPacked>(word, packed);
    llvm::Bitfield::set<MutableFlagInitialized>(word, true);
    identifiedBodySizeAndFlags.store(word, std::memory_order_release);

    return success();
  }

  /// Returns the key for the current storage.
  Key getAsKey() const {
    if (isIdentified() && isFrozen())
      return Key(Key::FrozenTag{}, getIdentifier(), isOpaque(),
                 getIdentifiedStructBody(), isPacked());
    if (isIdentified())
      return Key(getIdentifier(), isOpaque(), getIdentifiedStructBody());
    return Key(getTypeList(), isPacked());
  }

private:
  /// Returns the number of elements in the key.
  unsigned keySize() const {
    return llvm::Bitfield::get<KeySize>(keySizeAndFlags);
  }

  /// Sets the number of elements in the key.
  void setKeySize(unsigned value) {
    llvm::Bitfield::set<KeySize>(keySizeAndFlags, value);
  }

  /// Returns the word that holds the mutable component of identified structs.
  unsigned loadMutableWord() const {
    return identifiedBodySizeAndFlags.load(std::memory_order_acquire);
  }

  /// Bitfield elements for `keyAndSizeFlags`:
  ///   - bit 0: identified key flag;
  ///   - bit 1: packed key flag;
  ///   - bit 2: frozen key flag;
  ///   - bits 3..bitwidth(unsigned): size of the key.
  using KeyFlagIdentified =
      llvm::Bitfield::Element<bool, /*Offset=*/0, /*Size=*/1>;
  using KeyFlagPacked = llvm::Bitfield::Element<bool, /*Offset=*/1, /*Size=*/1>;
  using KeyFlagFrozen = llvm::Bitfield::Element<bool, /*Offset=*/2, /*Size=*/1>;
  using KeySize =
      llvm::Bitfield::Element<unsigned, /*Offset=*/3,
                              std::numeric_limits<unsigned>::digits - 3>;

  /// Bitfield elements for `identifiedBodySizeAndFlags`:
  ///   - bit 0: opaque flag;
  ///   - bit 1: packed mutable flag;
  ///   - bit 2: initialized flag;
  ///   - bits 3..bitwidth(unsigned): size of the identified body.
  using MutableFlagOpaque =
      llvm::Bitfield::Element<bool, /*Offset=*/0, /*Size=*/1>;
  using MutableFlagPacked =
      llvm::Bitfield::Element<bool, /*Offset=*/1, /*Size=*/1>;
  using MutableFlagInitialized =
      llvm::Bitfield::Element<bool, /*Offset=*/2, /*Size=*/1>;
  using MutableSize =
      llvm::Bitfield::Element<unsigned, /*Offset=*/3,
                              std::numeric_limits<unsigned>::digits - 3>;

  /// Pointer to the first element of the uniquing key.
  // Note: cannot use PointerUnion because bump-ptr allocator does not guarantee
  // address alignment.
  const void *keyPtr = nullptr;

  /// Pointer to the first type contained in an identified struct. Only valid
  /// once the initialized bit is set.
  const Type *identifiedBodyArray = nullptr;

  /// Size of the uniquing key combined with identified/literal and
  /// packedness bits. Must only be used through the Key* bitfields.
  unsigned keySizeAndFlags = 0;

  /// Number of the types contained in an identified struct combined with
  /// mutable flags. Must only be used through the Mutable* bitfields.
  std::atomic<unsigned> identifiedBodySizeAndFlags = 0;
};
} // end namespace detail
} // end namespace LLVM

/// Allow walking and replacing the subelements of a LLVMStructTypeStorage key.
template <>
struct AttrTypeSubElementHandler<LLVM::detail::LLVMStructTypeStorage::Key> {
  static void walk(const LLVM::detail::LLVMStructTypeStorage::Key &param,
                   AttrTypeImmediateSubElementWalker &walker) {
    if (param.isIdentified())
      walker.walkRange(param.getIdentifiedStructBody());
    else
      walker.walkRange(param.getTypeList());
  }
  static FailureOr<LLVM::detail::LLVMStructTypeStorage::Key>
  replace(const LLVM::detail::LLVMStructTypeStorage::Key &param,
          AttrSubElementReplacements &attrRepls,
          TypeSubElementReplacements &typeRepls) {
    using Key = LLVM::detail::LLVMStructTypeStorage::Key;
    if (param.isIdentified() && param.isFrozen())
      return Key(Key::FrozenTag{}, param.getIdentifier(), param.isOpaque(),
                 typeRepls.take_front(param.getIdentifiedStructBody().size()),
                 param.isPacked());
    // TODO: It's not clear how we support replacing sub-elements of mutable
    // types.
    if (param.isIdentified())
      return failure();

    return LLVM::detail::LLVMStructTypeStorage::Key(
        typeRepls.take_front(param.getTypeList().size()), param.isPacked());
  }
};

namespace LLVM {
namespace detail {
//===----------------------------------------------------------------------===//
// LLVMTypeAndSizeStorage.
//===----------------------------------------------------------------------===//

/// Common storage used for LLVM dialect types that need an element type and a
/// number: arrays, fixed and scalable vectors. The actual semantics of the
/// type is defined by its kind.
struct LLVMTypeAndSizeStorage : public TypeStorage {
  using KeyTy = std::tuple<Type, unsigned>;

  LLVMTypeAndSizeStorage(const KeyTy &key)
      : elementType(std::get<0>(key)), numElements(std::get<1>(key)) {}

  static LLVMTypeAndSizeStorage *construct(TypeStorageAllocator &allocator,
                                           const KeyTy &key) {
    return new (allocator.allocate<LLVMTypeAndSizeStorage>())
        LLVMTypeAndSizeStorage(key);
  }

  bool operator==(const KeyTy &key) const {
    return std::make_tuple(elementType, numElements) == key;
  }

  Type elementType;
  unsigned numElements;
};

} // namespace detail
} // namespace LLVM
} // namespace mlir

#endif // DIALECT_LLVMIR_IR_TYPEDETAIL_H
