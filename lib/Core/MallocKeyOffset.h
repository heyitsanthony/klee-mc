/* 
 * File:   MallocKeyOffset.h
 * Author: suhabe
 *
 * Created on April 19, 2010, 2:48 PM
 */

#ifndef _MALLOCKEYOFFSET_H
#define	_MALLOCKEYOFFSET_H


namespace klee {

    class MallocKeyOffset {
    public:
        MallocKey mallocKey;
        unsigned offset;

        MallocKeyOffset(const MallocKey& _mallocKey, unsigned _offset) : mallocKey(_mallocKey), offset(_offset) {

        }

        bool operator<(const MallocKeyOffset& a) const {
            return (mallocKey < a.mallocKey) ||
                    ((mallocKey == a.mallocKey) && (offset < a.offset));
        }

        bool operator==(const MallocKeyOffset &a) const {
            return !(a < *this) && !(*this < a);
        }

        bool operator!=(const MallocKeyOffset &a) const {
            return !(*this == a);
        }
    };
}
#endif	/* _MALLOCKEYOFFSET_H */

