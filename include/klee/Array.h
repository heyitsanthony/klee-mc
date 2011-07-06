/* stupid kludges to prevent future errors */
#ifndef KLEE_EXPR_H
#error Only include in expr.h
#endif

#ifdef KLEE_ARRAY_H
#error Only include once
#endif

#define KLEE_ARRAY_H

class Array {
private:
  unsigned int chk_val;
public:
  const std::string name;
  MallocKey mallocKey;

  /// constantValues - The constant initial values for this array, or empty for
  /// a symbolic array. If non-empty, this size of this array is equivalent to
  /// the array size.
  const std::vector< ref<ConstantExpr> > constantValues;
  // FIXME: This does not belong here.
  mutable void *stpInitialArray;
  mutable unsigned refCount;  // used only for const_arr's
  static const unsigned refCountDontCare = unsigned(-1);
  void initRef() const { refCount = 0; }
  void incRefIfCared() const { if (refCount != refCountDontCare) ++refCount; }
  void decRefIfCared() const { if (refCount != refCountDontCare) --refCount; if (refCount == 0) delete this; }

public:
  /// Array - Construct a new array object.
  ///
  /// \param _name - The name for this array. Names should generally be unique
  /// across an application, but this is not necessary for correctness, except
  /// when printing expressions. When expressions are printed the output will
  /// not parse correctly since two arrays with the same name cannot be
  /// distinguished once printed.
  Array(const std::string &_name,
        MallocKey _mallocKey,
        const ref<ConstantExpr> *constantValuesBegin = 0,
        const ref<ConstantExpr> *constantValuesEnd = 0)
    : name(_name), mallocKey(_mallocKey),
      constantValues(constantValuesBegin, constantValuesEnd),
      stpInitialArray(0), refCount(refCountDontCare)
  {
    chk_val = 0x12345678;
    assert((isSymbolicArray() || constantValues.size() == mallocKey.size) &&
           "Invalid size for constant array!");
#ifdef NDEBUG
    for (const ref<ConstantExpr> *it = constantValuesBegin;
         it != constantValuesEnd; ++it)
      assert(it->getWidth() == getRange() &&
             "Invalid initial constant value!");
#endif
  }
  ~Array();

  bool isSymbolicArray() const { assert (chk_val == 0x12345678); return constantValues.empty(); }
  bool isConstantArray() const { return !isSymbolicArray(); }

  Expr::Width getDomain() const { return Expr::Int32; }
  Expr::Width getRange() const { return Expr::Int8; }

  const char* getSTPArrayName() const {
    if (!stpInitialArray)
      return NULL;
    const char* stpName = exprName(stpInitialArray);
    assert(stpName);
    return stpName;
  }

  // returns true if a < b
  bool operator< (const Array &b) const
  {
    if (isConstantArray() != b.isConstantArray()) {
      return isConstantArray() < b.isConstantArray();
    } else if (isConstantArray() && b.isConstantArray()) {
      // disregard mallocKey for constant arrays; mallocKey matches are 
      // not a sufficient condition for constant arrays, but value matches are
      if (constantValues.size() != b.constantValues.size())
        return constantValues.size() < b.constantValues.size();

      for (unsigned i = 0; i < constantValues.size(); i++) {
        if (constantValues[i] != b.constantValues[i])
          return constantValues[i] < b.constantValues[i];
      }
      return false; // equal, so NOT less than
    } else if (mallocKey.allocSite && b.mallocKey.allocSite) {
      return mallocKey.compare(b.mallocKey) == -1;
    } else {
      return name < b.name;
    }
  }

  struct Compare
  {
    // returns true if a < b
    bool operator()(const Array* a, const Array* b) const
    {
      return (*a < *b);
    }
  };
};
