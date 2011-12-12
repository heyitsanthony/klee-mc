#ifndef KLEE_TWOOSTREAMS_H
#define KLEE_TWOOSTREAMS_H

#include <iostream>

struct TwoOStreams
{
    std::ostream* s[2];

    TwoOStreams(std::ostream* s0, std::ostream* s1) { s[0] = s0; s[1] = s1; }

    template <typename T>
    TwoOStreams& operator<<(const T& t) {
        *s[0] << t;
        *s[1] << t;
        return *this;
    }

    TwoOStreams& operator<<(std::ostream& (*f)(std::ostream&)) {
        *s[0] << f;
        *s[1] << f;
        return *this;
    }
};

class PrefixWriter
{
    TwoOStreams* streams;
    const char* prefix;

public:
    PrefixWriter(TwoOStreams& s, const char* p) : streams(&s), prefix(p) { }

    operator TwoOStreams&() const {
        *streams->s[0] << prefix;
        return *streams;
    }

    template <typename T>
    TwoOStreams& operator<<(const T& t) {
        static_cast<TwoOStreams&>(*this) << t;
        return *streams;
    }

    TwoOStreams& operator<<(std::ostream& (*f)(std::ostream&)) {
        static_cast<TwoOStreams&>(*this) << f;
        return *streams;
    }
};


#endif
