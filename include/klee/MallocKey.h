#ifndef KLEE_MALLOCKEY_H
#define KLEE_MALLOCKEY_H

#ifndef KLEE_EXPR_H
#error Should only be included from expr.h
#endif

class MallocKey
{
public:
  typedef std::map<MallocKey,std::set<uint64_t> > seensizes_ty;
  static seensizes_ty seenSizes;

  /// "Location" for which this memory object was allocated. This
  /// should be either the allocating instruction or the global object
  /// it was allocated for (or whatever else makes sense).
  const llvm::Value* allocSite;

  /// Within the state where this malloc() call occurred, # of times malloc()
  /// was called at this allocSite before the call that generated this key.
  unsigned iteration;
  unsigned size;
  mutable unsigned hash_v;
  bool isLocal;
  bool isGlobal;
  bool isFixed;

  MallocKey(const llvm::Value* _allocSite, unsigned _iteration,
                 uint64_t _size, bool _isLocal, bool _isGlobal,
                 bool _isFixed)
    : allocSite(_allocSite), iteration(_iteration), size(_size)
    , hash_v(0)
    , isLocal(_isLocal), isGlobal(_isGlobal), isFixed(_isFixed) { }

  MallocKey(uint64_t _size)
    : allocSite(0), iteration(0), size(_size)
    , hash_v(0)
    , isLocal(false), isGlobal(false), isFixed(false) { }

  /// Compares two MallocKeys; used for ordering MallocKeys within STL
  /// containers
  /// NOTE: not sufficient to assume two MallocKeys are compatible just because
  /// !(a < b) && !(b < a). MUST verify that a.compare(b) == 0 because size is
  /// not used here (to maximize cache hit rate).
  bool operator<(const MallocKey& a) const {
    return (allocSite < a.allocSite) ||
           (allocSite == a.allocSite && iteration < a.iteration);
  }
  bool operator==(const MallocKey &a) const {
    if (&a == this) return true;
    return !(a < *this) && !(*this < a);
  }
  bool operator!=(const MallocKey &a) const { return !(*this == a); }

  /// Compare two MallocKeys
  /// Returns:
  ///  0 if allocSite and iteration match and size >= a.size and
  ///    size <= a.size's lower bound
  ///  -1 if allocSite or iteration do not match and operator< returns true or
  ///     allocSite and iteration match but size < a.size
  ///  1  if allocSite or iteration do not match and operator< returns false or
  ///     allocSite and iteration match but size > a.size's lower bound
  int compare(const MallocKey &a) const;
  unsigned hash() const;
};

#endif
