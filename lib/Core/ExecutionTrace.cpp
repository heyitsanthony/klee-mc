#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/ExecutionState.h"

using namespace klee;

ExecutionTraceEvent::ExecutionTraceEvent(ExecutionState& state,
                                         KInstruction* ki)
  : consecutiveCount(1)
{
  file = ki->info->file;
  line = ki->info->line;
  funcName = state.getCurrentKFunc()->function->getName();
  stackDepth = state.stack.size();
}

bool ExecutionTraceEvent::ignoreMe() const {
  // ignore all events occurring in certain pesky uclibc files:
  if (file.find("libc/stdio/") != std::string::npos) {
    return true;
  }

  return false;
}

void ExecutionTraceEvent::print(std::ostream &os) const {
  os.width(stackDepth);
  os << ' ';
  printDetails(os);
  os << ' ' << file << ':' << line << ':' << funcName;
  if (consecutiveCount > 1)
    os << " (" << consecutiveCount << "x)\n";
  else
    os << '\n';
}

bool ExecutionTraceEventEquals(ExecutionTraceEvent* e1, ExecutionTraceEvent* e2) {
  // first see if their base class members are identical:
  if (!((e1->file == e2->file) &&
        (e1->line == e2->line) &&
        (e1->funcName == e2->funcName)))
    return false;

  // fairly ugly, but i'm no OOP master, so this is the way i'm
  // doing it for now ... lemme know if there's a cleaner way:
  BranchTraceEvent* be1 = dynamic_cast<BranchTraceEvent*>(e1);
  BranchTraceEvent* be2 = dynamic_cast<BranchTraceEvent*>(e2);
  if (be1 && be2) {
    return ((be1->trueTaken == be2->trueTaken) &&
            (be1->canForkGoBothWays == be2->canForkGoBothWays));
  }

  // don't tolerate duplicates in anything else:
  return false;
}


void BranchTraceEvent::printDetails(std::ostream &os) const {
  os << "BRANCH " << (trueTaken ? "T" : "F") << ' ' <<
        (canForkGoBothWays ? "2-way" : "1-way");
}

void ExecutionTraceManager::addEvent(ExecutionTraceEvent* evt) {
  // don't trace anything before __user_main, except for global events
  if (!hasSeenUserMain) {
    if (evt->funcName == "__user_main") {
      hasSeenUserMain = true;
    }
    else if (evt->funcName != "global_def") {
      return;
    }
  }

  // custom ignore events:
  if (evt->ignoreMe())
    return;

  if (events.size() > 0) {
    // compress consecutive duplicates:
    ExecutionTraceEvent* last = events.back();
    if (ExecutionTraceEventEquals(last, evt)) {
      last->consecutiveCount++;
      return;
    }
  }

  events.push_back(evt);
}

void ExecutionTraceManager::printAllEvents(std::ostream &os) const {
  for (unsigned i = 0; i != events.size(); ++i)
    events[i]->print(os);
}
