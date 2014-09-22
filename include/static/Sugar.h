#ifndef __SUGAR__
#define __SUGAR__

#include <cstring>

// Lame C++ sugar.  I'm pretty sure there better definitions.

#define let(_lhs, _rhs)  typeof(_rhs) _lhs = _rhs

#define foreach(_i, _b, _e) \
	  for(typeof(_b) _i = _b, _i ## end = _e; _i != _i ## end;  ++ _i)

#define foreach_manual(_i, _b, _e) \
	  for(typeof(_b) _i = _b, _i ## end = _e; _i != _i ## end;)

#define foreach_T(T, _i, _b, _e) \
	  for(typename T _i = _b, _i ## end = _e; _i != _i ## end;  ++ _i)

#define forall_drain(_i, _b, _e)	\
	  for(typeof(_b) _i = _b, _i ## end = _e; _i != _i ## end;  _i = _b)


/* one day we'll have C++11 and have std::remove_const */
template< class T > struct remove_const          { typedef T type; };
template< class T > struct remove_const<const T> { typedef T type; };

#define unconst_key_T(x)	remove_const<typeof(x.begin()->first)>::type

struct ltstr
{
    bool operator()(const char* str1, const char* str2) const
    {
        return std::strcmp(str1, str2) < 0;
    }
};


#endif /* __SUGAR__ */

