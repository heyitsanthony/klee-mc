#ifndef _SLICER_ORDEREDSET_H
#define	_SLICER_ORDEREDSET_H

#include <list>
#include <set>

template <typename V>
class OrderedSet {
public:
    OrderedSet()  {
    }

    typename std::list<V>::iterator begin() {
        return list.begin();
    }

    typename std::list<V>::iterator end() {
        return list.end();
    }

    bool empty() {
        return set.empty();
    }

    unsigned int size() {
        return set.size();
    }

    bool contains(V v) {
        return set.find(v) != set.end();
    }

    V pop() {
        V v = list.front();
        list.pop_front();
        set.erase(v);
        return v;
    }

    void add(V v) {
        if (set.find(v) != set.end()) 
            return;        
        
        list.push_back(v);
        set.insert(v);
    }


private:
    typename std::list<V> list;
    typename std::set<V> set;

};

#endif	

