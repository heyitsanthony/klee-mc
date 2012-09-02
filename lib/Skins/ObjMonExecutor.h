/**
 * main idea behind this one is to track when constraints are
 * added to the 
 *
 */
#ifndef OBJMONEXE_H
#define OBJMONEXE_H

namespace klee
{
template<typename T>
class ObjMonExecutor : public T
{};
}

#endif