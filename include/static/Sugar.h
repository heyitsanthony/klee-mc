#ifndef __SUGAR__
#define __SUGAR__

#include <cstring>

// Lame C++ sugar.  I'm pretty sure there better definitions.

#define let(_lhs, _rhs)  typeof(_rhs) _lhs = _rhs

#define foreach(_i, _b, _e) \
	  for(auto  _i = _b, _i ## end = _e; _i != _i ## end;  ++ _i)

#define foreach_manual(_i, _b, _e) \
	  for(auto  _i = _b, _i ## end = _e; _i != _i ## end;)

#define foreach_T(T, _i, _b, _e) \
	  for(typename T _i = _b, _i ## end = _e; _i != _i ## end;  ++ _i)

#define forall_drain(_i, _b, _e)	\
	  for(auto _i = _b, _i ## end = _e; _i != _i ## end;  _i = _b)

#define unconst_key_T(x)	std::remove_const<decltype(x.begin()->first)>::type

struct ltstr
{
    bool operator()(const char* str1, const char* str2) const
    {
        return std::strcmp(str1, str2) < 0;
    }
};


#include <vector>
#include <list>
#include <memory>
template<typename T> using ptr_vec_t = std::vector<std::unique_ptr<T>>;
template<typename T> using ptr_list_t = std::list<std::unique_ptr<T>>;

#endif /* __SUGAR__ */

