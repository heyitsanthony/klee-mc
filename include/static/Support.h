/* -*- mode: c++; c-basic-offset: 4; -*- */

#ifndef _SLICER_UTIL_SLICERUTILS_H
#define	_SLICER_UTIL_SLICERUTILS_H

#include <sstream>
#include "llvm/Value.h"
#include <string>
#include <algorithm>
#include <klee/Expr.h>
#include <llvm/Support/raw_os_ostream.h>
#include "static/Sugar.h"
#include <iostream>

using namespace llvm;

namespace klee {

    class Support {
    public:
	template <class T>
	static std::string printStr(T& t)
	{
		std::stringstream ss;
		t.print(ss);
		return ss.str();
	}

	template <class T>
	static std::string printStr(const T& t)
	{
		std::stringstream ss;
		t.print(ss);
		return ss.str();
	}

        template <class T>
        static void eraseAll(std::set<T>& s, const std::vector<T>& rm) {

            foreach(it, rm.begin(), rm.end()) {
                s.erase(*it);
            }
        }

        template <class T>
        static bool isDisjoint(const std::set<T>& s1, const std::set<T>& s2) {

            foreach(it, s1.begin(), s1.end()) {
                T rec = *it;
                if (s2.count(rec)) {
                    return false;
                }
            }

            return true;
        }

        static void print(const std::set<unsigned>& s) {

            foreach(it, s.begin(), s.end()) {
                unsigned s = *it;
                std::cout << s << " ";
            }
        }

        static bool eq(ref<Expr> e1, ref<Expr> e2) {
            if (e1.isNull() && e2.isNull())
                return true;

            if (e1.isNull() || e2.isNull())
                return false;

            return e1 == e2;
        }

        static std::string str(ref<Expr> e) {
            if (e.isNull())
                return "null";
            std::string s;
            std::stringstream out;
            out << *e;
            s = out.str();
            return s;
        }

        static inline std::string & ltrim(std::string & s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
            return s;
        }

        // trim from end

        static inline std::string & rtrim(std::string & s) {
            s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
            return s;
        }

        static inline std::string & trim(std::string & s) {
            return ltrim(rtrim(s));
        }

        static std::string str(Value * i) {
		std::stringstream out;
		raw_os_ostream	ros(out);
		i->print(ros);
		ros.flush();
		std::string s(out.str());
		return trim(s);
        }

        static std::string str(int i) {
            std::string s;
            std::stringstream out;
            out << i;
            s = out.str();
            return s;
        }


    };
}

#endif

