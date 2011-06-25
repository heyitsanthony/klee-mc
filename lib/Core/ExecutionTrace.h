#ifndef KLEE_EXETRACE_H
#define KLEE_EXETRACE_H

#include <vector>
#include <iostream>

namespace klee
{

class ExecutionTraceEvent;
class ExecutionState;
class KInstruction;

// each state should have only one of these guys ...
class ExecutionTraceManager {
public:
  ExecutionTraceManager() : hasSeenUserMain(false) {}

  void addEvent(ExecutionTraceEvent* evt);
  void printAllEvents(std::ostream &os) const;

private:
  // have we seen a call to __user_main() yet?
  // don't bother tracing anything until we see this,
  // or else we'll get junky prologue shit
  bool hasSeenUserMain;

  // ugh C++ only supports polymorphic calls thru pointers
  //
  // WARNING: these are NEVER FREED, because they are shared
  // across different states (when states fork), so we have 
  // an *intentional* memory leak, but oh wellz ;)
  std::vector<ExecutionTraceEvent*> events;
};

// for producing abbreviated execution traces to help visualize
// paths and diagnose bugs

class ExecutionTraceEvent
{
public:
  // the location of the instruction:
  std::string file;
  unsigned line;
  std::string funcName;
  unsigned stackDepth;

  unsigned consecutiveCount; // init to 1, increase for CONSECUTIVE
                             // repetitions of the SAME event

  enum Kind {
    Invalid = -1,
    FunctionCall = 0,
    FunctionReturn,
    Branch
  };

  ExecutionTraceEvent()
    : file("global"), line(0), funcName("global_def"),
      consecutiveCount(1) {}

  ExecutionTraceEvent(ExecutionState& state, KInstruction* ki);

  virtual ~ExecutionTraceEvent() {}

  virtual Kind getKind(void) const = 0;

  void print(std::ostream &os) const;

  // return true if it shouldn't be added to ExecutionTraceManager
  //
  virtual bool ignoreMe() const;

  static bool classof(const ExecutionTraceEvent* ) { return true; }

private:
  virtual void printDetails(std::ostream &os) const = 0;
};


class FunctionCallTraceEvent : public ExecutionTraceEvent {
public:
  std::string calleeFuncName;

  FunctionCallTraceEvent(ExecutionState& state, KInstruction* ki,
                         const std::string& _calleeFuncName)
    : ExecutionTraceEvent(state, ki), calleeFuncName(_calleeFuncName) {}

  Kind getKind() const { return FunctionCall; }

  static bool classof(const ExecutionTraceEvent* e) {
  	return e->getKind() == FunctionCall;
  }

  static bool classof(const FunctionCallTraceEvent*) { return true; }

private:
  virtual void printDetails(std::ostream &os) const {
    os << "CALL " << calleeFuncName;
  }

};

class FunctionReturnTraceEvent : public ExecutionTraceEvent {
public:
  FunctionReturnTraceEvent(ExecutionState& state, KInstruction* ki)
    : ExecutionTraceEvent(state, ki) {}

  Kind getKind() const { return FunctionReturn; }

  static bool classof(const ExecutionTraceEvent *E) {
    return E->getKind() == FunctionReturn;
  }
  static bool classof(const FunctionReturnTraceEvent *) { return true; }

private:
  virtual void printDetails(std::ostream &os) const {
    os << "RETURN";
  }
};

class BranchTraceEvent : public ExecutionTraceEvent {
public:
  bool trueTaken;         // which side taken?
  bool canForkGoBothWays;

  BranchTraceEvent(ExecutionState& state, KInstruction* ki,
                   bool _trueTaken, bool _isTwoWay)
    : ExecutionTraceEvent(state, ki),
      trueTaken(_trueTaken),
      canForkGoBothWays(_isTwoWay) {}

  Kind getKind() const { return Branch; }

  static bool classof(const BranchTraceEvent *) { return true; }

  static bool classof(const ExecutionTraceEvent *E) {
    return E->getKind() == Branch;
  }

private:
  virtual void printDetails(std::ostream &os) const;
};
}

#endif
