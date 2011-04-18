#ifndef _LOCATION_H
#define	_LOCATION_H

#include <stdlib.h>
#include "klee/Expr.h"
#include "MallocKeyOffset.h"

namespace klee {

    class Location {
    public:

        enum LocationType {
            STACK, OBJBYTE, OBJ, ARR, ARRBYTE
        };

        unsigned type;

        Location(unsigned _type) : type(_type) {

        }

        std::string typestr() {
            switch (type) {
                case STACK:
                    return "stack";
                case OBJBYTE:
                    return "objbyte";
                case OBJ:
                    return "obj";
                case ARR:
                    return "arr";
                case ARRBYTE:
                    return "arrbyte";
                default:
                    assert(false);
            }
        }

    };

    class StackLocation : public Location {
    public:
        unsigned sfi;
        unsigned reg;

        StackLocation(unsigned _sfi, unsigned _reg) : Location(STACK), sfi(_sfi), reg(_reg) {
        }

        bool operator<(const StackLocation& a) const {
            return (sfi < a.sfi) ||
                    ((sfi == a.sfi) && (reg < a.reg));
        }
    };

    class ObjectByteLocation : public Location {
    public:
        MallocKey mallocKey;
        unsigned offset;

        ObjectByteLocation(MallocKey _mallocKey, unsigned _offset) : Location(OBJBYTE), mallocKey(_mallocKey), offset(_offset) {
        }

        bool operator<(const ObjectByteLocation& a) const {
            return (mallocKey < a.mallocKey) ||
                    ((mallocKey == a.mallocKey) && (offset < a.offset));
        }

    };

    class ObjectLocation : public Location {
    public:
        MallocKey mallocKey;

        ObjectLocation(MallocKey _mallocKey) : Location(OBJ), mallocKey(_mallocKey) {
        }

        bool operator<(const ObjectLocation& a) const {
            return (mallocKey < a.mallocKey);
        }

    };

    class ArrayByteLocation : public Location {
    public:
        MallocKeyOffset mallocKeyOffset;
        
        ArrayByteLocation(MallocKeyOffset _mallocKeyOffset) : Location(ARRBYTE), mallocKeyOffset(_mallocKeyOffset) {
        }

        bool operator<(const ArrayByteLocation& a) const {
            return (mallocKeyOffset < a.mallocKeyOffset);
        }
    };

    class ArrayLocation : public Location {
    public:
        MallocKey mallocKey;

        ArrayLocation(MallocKey _mallocKey) : Location(ARR), mallocKey(_mallocKey) {
        }

        bool operator<(const ArrayLocation& a) const {
            return (mallocKey < a.mallocKey);
        }
    };
}

#endif

