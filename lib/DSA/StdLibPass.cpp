//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Recognize common standard c library functions and generate graphs for them
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "static/dsa/DataStructure.h"
#include "static/dsa/DSGraph.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Timer.h"
#include <iostream>
#include "llvm/IR/Module.h"

#include <set>

using namespace llvm;

static RegisterPass<StdLibDataStructures>
X("dsa-stdlib", "Standard Library Local Data Structure Analysis");

char StdLibDataStructures::ID;

#define numOps 10

struct libAction {
  bool read[numOps];
  bool write[numOps];
  bool heap[numOps];
  bool mergeAllArgs;
  bool mergeWithRet;
  bool collapse;
};

#define NRET_NARGS  {0,0,0,0,0,0,0,0,0,0}
#define YRET_NARGS  {1,0,0,0,0,0,0,0,0,0}
#define NRET_YARGS  {0,1,1,1,1,1,1,1,1,1}
#define YRET_YARGS  {1,1,1,1,1,1,1,1,1,1}
#define NRET_NYARGS {0,0,1,1,1,1,1,1,1,1}
#define NRET_NNYARGS {0,0,0,1,1,1,1,1,1,1}
#define NRET_NNYNARGS {0,0,0,1,0,1,1,1,1,1}
#define NRET_NYNARGS {0,0,1,0,1,1,1,1,1,1}
#define YRET_NYARGS {1,0,1,1,1,1,1,1,1,1}
#define NRET_YNARGS {0,1,0,0,0,0,0,0,0,0}
#define YRET_YNARGS {1,1,0,0,0,0,0,0,0,0}
#define NRET_YYNARGS {0,1,1,0,0,0,0,0,0,0}


const struct {
  const char* name;
  libAction action;
} recFuncs[] = {
  //SUHABE: start additions.
  {"fcntl",      	{NRET_NNYARGS,  NRET_NNYARGS, NRET_NARGS, false, false, false}},
  {"gethostname",	{NRET_YNARGS,	NRET_NARGS, 	NRET_NARGS, false, false, false}},
  {"ioctl", 		{NRET_NNYARGS,	NRET_NNYARGS, NRET_NARGS, false, false, false}},
  {"llvm.memcpy.i32",   {NRET_NYARGS,	NRET_YNARGS, NRET_NARGS, true, false, true}},
  {"llvm.memcpy.i64",   {NRET_NYARGS,	NRET_YNARGS, NRET_NARGS, true, false, true}},
  {"llvm.memmove.i32",  {NRET_NYARGS,	NRET_YNARGS, NRET_NARGS, true, false, true}},
  {"llvm.memmove.i64",  {NRET_NYARGS,	NRET_YNARGS, NRET_NARGS, true, false, true}},
  {"llvm.memset.i32",   {NRET_NARGS,	NRET_YNARGS, NRET_NARGS, false, false, true}},
  {"llvm.memset.i64",   {NRET_NARGS,	NRET_YNARGS, NRET_NARGS, false, false, true}},
  {"llvm.va_copy", 	{NRET_NYARGS,	NRET_YNARGS, NRET_NARGS, true, false, true}},
  {"llvm.va_end", 	{NRET_NARGS,	NRET_YARGS, NRET_NARGS, false, false, false}},
  {"llvm.va_start",	{NRET_NARGS,	NRET_YARGS, NRET_NARGS, false, false, false}},
  {"open",       	{NRET_YNARGS, 	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"read", 		{NRET_NARGS,	NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"select", 		{NRET_NYARGS,	NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"sigprocmask", 	{NRET_NYNARGS,	NRET_NNYARGS, NRET_NARGS, false, false, false}},
  {"__socketcall",	{NRET_NYARGS,	NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"__syscall_rt_sigaction", {NRET_NYNARGS,NRET_NNYNARGS, NRET_NARGS, false, false, false}},
  {"vprintf", 		{NRET_YARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"write", 		{NRET_NYNARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"getcwd",            {YRET_YNARGS,	YRET_YNARGS, YRET_NARGS, false, false, false}},
  {"readlink",          {NRET_YNARGS,	NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"frexp",             {NRET_NARGS,	NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"strcasecmp_l",       {NRET_YYNARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"iswctype_l",       {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},

  {"gettimeofday",	{NRET_YYNARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},  
  {"__xstat64",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"__assert_fail",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"syscall",            {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},

  {"klee_warning",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_warning_once",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_report_error",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},  
  {"klee_check_memory_access",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_mark_global",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_make_symbolic",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_prefer_cex",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
  {"klee_range",         {NRET_NARGS,	NRET_NARGS, NRET_NARGS, false, false, false}},
//SUHABE: end additions.


  {"stat",       {NRET_YNARGS, NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"fstat",      {NRET_YNARGS, NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"lstat",      {NRET_YNARGS, NRET_NYARGS, NRET_NARGS, false, false, false}},
  // printf not strictly true, %n could cause a write
  //SUHABE: not sound because printf may have unbound number of operands
  {"printf",     {NRET_YARGS,  NRET_NARGS,  NRET_NARGS, false, false, false}},
  //SUHABE: why does fprintf write it's first argument?
  {"fprintf",    {NRET_YARGS,  NRET_YNARGS, NRET_NARGS, false, false, false}},
  {"sprintf",    {NRET_YARGS,  NRET_YNARGS, NRET_NARGS, false, false, false}},
  {"snprintf",   {NRET_YARGS,  NRET_YNARGS, NRET_NARGS, false, false, false}},
  {"fprintf",    {NRET_YARGS,  NRET_YNARGS, NRET_NARGS, false, false, false}},
  {"puts",       {NRET_YARGS,  NRET_NARGS,  NRET_NARGS, false, false, false}},
  {"putc",       {NRET_NARGS,  NRET_NARGS,  NRET_NARGS, false, false, false}},
  {"putchar",    {NRET_NARGS,  NRET_NARGS,  NRET_NARGS, false, false, false}},
  {"fputs",      {NRET_YARGS,  NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"fputc",      {NRET_YARGS,  NRET_NYARGS, NRET_NARGS, false, false, false}},

  {"calloc",     {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  {"malloc",     {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  {"valloc",     {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  {"memalign",   {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  {"realloc",    {NRET_NARGS, YRET_NARGS, YRET_YNARGS, false,  true,  true}},
  {"free",       {NRET_NARGS, NRET_NARGS, NRET_YNARGS,  false, false, false}},
  
  {"strdup",     {NRET_YARGS, YRET_NARGS, YRET_NARGS,  false, true, false}},
  {"wcsdup",     {NRET_YARGS, YRET_NARGS, YRET_NARGS,  false, true, false}},

  {"atoi",       {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"atof",       {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"atol",       {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"atoll",      {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"atoq",       {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},

  {"memcmp",     {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"strcmp",     {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"wcscmp",     {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"strncmp",    {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"wcsncmp",    {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"strcasecmp", {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"wcscasecmp", {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"strncasecmp",{NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"wcsncasecmp",{NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"strlen",     {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
  {"wcslen",     {NRET_YARGS, NRET_NARGS, NRET_NARGS, false, false, false}},

  {"memchr",     {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"wmemchr",    {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"memrchr",    {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"strchr",     {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"wcschr",     {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"strrchr",    {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"wcsrchr",    {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"strchrhul",  {YRET_YARGS, NRET_NARGS, NRET_NARGS, false, true, true}},
  {"strcat",     {YRET_YARGS, YRET_YARGS, NRET_NARGS,  true, true, true}},
  {"strncat",    {YRET_YARGS, YRET_YARGS, NRET_NARGS,  true, true, true}},

  {"strcpy",     {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},
  {"memcpy",     {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},
  {"memset",     {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},
  {"wcscpy",     {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},
  {"strcpy",     {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},
  {"wcsncpy",    {YRET_YARGS, YRET_YARGS, NRET_NARGS, true, true, true}},


  {"fwrite",     {NRET_YARGS, NRET_NYARGS, NRET_NARGS, false, false, false}},
  {"fread",      {NRET_NYARGS, NRET_YARGS, NRET_NARGS, false, false, false}},
  {"fflush",     {NRET_YARGS,  NRET_YARGS, NRET_NARGS, false, false, false}},
  {"fclose",     {NRET_YARGS,  NRET_YARGS, NRET_NARGS, false, false, false}},
  {"fopen",      {NRET_YARGS,  YRET_NARGS, YRET_NARGS, false, false, false}},
  {"fileno",     {NRET_YARGS,  NRET_NARGS, NRET_NARGS, false, false, false}},
  {"unlink",     {NRET_YARGS,  NRET_NARGS, NRET_NARGS, false, false, false}},

  {"perror",     {NRET_YARGS,  NRET_NARGS, NRET_NARGS, false, false, false}},  
  
#if 0
  {"remove",     {false, false, false,  true, false, false, false, false, false}},
  {"unlink",     {false, false, false,  true, false, false, false, false, false}},
  {"rename",     {false, false, false,  true, false, false, false, false, false}},
  {"memcmp",     {false, false, false,  true, false, false, false, false, false}},
  {"execl",      {false, false, false,  true, false, false, false, false, false}},
  {"execlp",     {false, false, false,  true, false, false, false, false, false}},
  {"execle",     {false, false, false,  true, false, false, false, false, false}},
  {"execv",      {false, false, false,  true, false, false, false, false, false}},
  {"execvp",     {false, false, false,  true, false, false, false, false, false}},
  {"chmod",      {false, false, false,  true, false, false, false, false, false}},
  {"puts",       {false, false, false,  true, false, false, false, false, false}},
  {"write",      {false, false, false,  true, false, false, false, false, false}},
  {"open",       {false, false, false,  true, false, false, false, false, false}},
  {"create",     {false, false, false,  true, false, false, false, false, false}},
  {"truncate",   {false, false, false,  true, false, false, false, false, false}},
  {"chdir",      {false, false, false,  true, false, false, false, false, false}},
  {"mkdir",      {false, false, false,  true, false, false, false, false, false}},
  {"rmdir",      {false, false, false,  true, false, false, false, false, false}},
  {"read",       {false, false, false, false,  true, false, false, false, false}},
  {"pipe",       {false, false, false, false,  true, false, false, false, false}},
  {"wait",       {false, false, false, false,  true, false, false, false, false}},
  {"time",       {false, false, false, false,  true, false, false, false, false}},
  {"getrusage",  {false, false, false, false,  true, false, false, false, false}},
  {"memmove",    {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"bcopy",      {false, false, false,  true,  true, false,  true, false,  true}},
  {"strcpy",     {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"strncpy",    {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"memccpy",    {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"wcscpy",     {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"wcsncpy",    {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"wmemccpy",   {false,  true, false,  true,  true, false,  true,  true,  true}},
  {"getcwd",     { true,  true,  true,  true,  true,  true, false,  true,  true}},
#endif
  // C++ functions, as mangled on linux gcc 4.2
  // operator new(unsigned long)
  {"_Znwm",      {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  // operator new[](unsigned long)
  {"_Znam",      {NRET_NARGS, YRET_NARGS, YRET_NARGS,  false, false, false}},
  // operator delete(void*)
  {"_ZdlPv",     {NRET_NARGS, NRET_NARGS, NRET_YNARGS,  false, false, false}},
  // operator delete[](void*)
  {"_ZdaPv",     {NRET_NARGS, NRET_NARGS, NRET_YNARGS,  false, false, false}},
  // Terminate the list of special functions recognized by this pass
  {0,            {NRET_NARGS, NRET_NARGS, NRET_NARGS, false, false, false}},
};

void StdLibDataStructures::eraseCallsTo(Function* F) {
  for (Value::use_iterator ii = F->use_begin(), ee = F->use_end();
       ii != ee; ++ii)
    if (CallInst* CI = dyn_cast<CallInst>(*ii))
      if (CI->getOperand(0) == F) {
        DSGraph* Graph = getDSGraph(*CI->getParent()->getParent());
        //delete the call
        DEBUG(errs() << "Removing " << F->getName().str() << " from " 
	      << CI->getParent()->getParent()->getName().str() << "\n");
        Graph->removeFunctionCalls(*F);
      }
}

bool StdLibDataStructures::runOnModule(Module &M) {	
  init(&getAnalysis<LocalDataStructures>(), false, true, false, false);

  std::set<Function*> summarized;
  
  //Clone Module
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
	  if (!I->isDeclaration()) {		  
		  getOrCreateGraph(&*I);
	  }
  }

  //Trust the readnone annotation
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) { 
	  if (I->isDeclaration() && I->doesNotAccessMemory() && !isa<PointerType>(I->getReturnType())) {
		  summarized.insert(&*I);
		  eraseCallsTo(I);
	  }
  }

  //Useless external
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) { 
	  if (I->isDeclaration() && !I->isVarArg() && !isa<PointerType>(I->getReturnType())) {		  		  
		  bool hasPtr = false;
		  
		  for (Function::arg_iterator ii = I->arg_begin(), ee = I->arg_end(); ii != ee; ++ii) {
			  if (isa<PointerType>(ii->getType())) {
				  hasPtr = true;
				  break;
			  }
		  }
		  
		  if (!hasPtr) {
			  summarized.insert(&*I);
			  eraseCallsTo(I);
		  }
	  }
  }

  //Functions we handle by summary
  for (int x = 0; recFuncs[x].name; ++x) { 
	  if (Function* F = M.getFunction(recFuncs[x].name)) {
		  if (F->isDeclaration()) {
			  summarized.insert(F);
		  }
	  }
  }
  
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I) {
	  Function* f = &*I;
	  if (f->isDeclaration() && (summarized.find(f) == summarized.end())) {
		  std::cout << "ERROR: Declaration function aliasing effects not summarized: " << f->getName().str() << "\n";
	  }
	  
	  if (!f->isDeclaration() && (summarized.find(f) != summarized.end())) {
		  std::cout << "ERROR: Non-declaration function aliasing effect summarized " << f->getName().str() << "\n";
	  }
  }
  
  for (int x = 0; recFuncs[x].name; ++x) 
    if (Function* F = M.getFunction(recFuncs[x].name))
      if (F->isDeclaration()) {
        for (Value::use_iterator ii = F->use_begin(), ee = F->use_end();
             ii != ee; ++ii) {
	  CallInst* CI = dyn_cast<CallInst>(*ii);
          if (CI != NULL)
            if (CI->getOperand(0) == F) {
              DSGraph* Graph = getDSGraph(*CI->getParent()->getParent());
              if (recFuncs[x].action.read[0])
                Graph->getNodeForValue(CI).getNode()->setReadMarker();
              if (recFuncs[x].action.write[0])
                Graph->getNodeForValue(CI).getNode()->setModifiedMarker();
              if (recFuncs[x].action.heap[0])
                Graph->getNodeForValue(CI).getNode()->setHeapMarker();

              for (unsigned y = 1; y < CI->getNumOperands(); ++y)
                if (recFuncs[x].action.read[y])
                  if (isa<PointerType>(CI->getOperand(y)->getType()))
                    if (DSNode * Node=Graph->getNodeForValue(CI->getOperand(y)).getNode())
                      Node->setReadMarker();
              for (unsigned y = 1; y < CI->getNumOperands(); ++y)
                if (recFuncs[x].action.write[y])
                  if (isa<PointerType>(CI->getOperand(y)->getType()))
                    if (DSNode * Node=Graph->getNodeForValue(CI->getOperand(y)).getNode())
                      Node->setModifiedMarker();
              for (unsigned y = 1; y < CI->getNumOperands(); ++y)
                if (recFuncs[x].action.heap[y])
                  if (isa<PointerType>(CI->getOperand(y)->getType()))
                    if (DSNode * Node=Graph->getNodeForValue(CI->getOperand(y)).getNode())
                      Node->setHeapMarker();

              std::vector<DSNodeHandle> toMerge;
              if (recFuncs[x].action.mergeWithRet)
                toMerge.push_back(Graph->getNodeForValue(CI));
              if (recFuncs[x].action.mergeAllArgs || recFuncs[x].action.mergeWithRet)
                for (unsigned y = 1; y < CI->getNumOperands(); ++y)
                  if (isa<PointerType>(CI->getOperand(y)->getType()))
                    toMerge.push_back(Graph->getNodeForValue(CI->getOperand(y)));
              for (unsigned y = 1; y < toMerge.size(); ++y)
                toMerge[0].mergeWith(toMerge[y]);

              if (recFuncs[x].action.collapse) {
                if (isa<PointerType>(CI->getType()))
                  Graph->getNodeForValue(CI).getNode()->foldNodeCompletely();
                for (unsigned y = 1; y < CI->getNumOperands(); ++y)
                  if (isa<PointerType>(CI->getOperand(y)->getType()))
                    if (DSNode * Node=Graph->getNodeForValue(CI->getOperand(y)).getNode())
                      Node->foldNodeCompletely();
              }
            }
	}
        eraseCallsTo(F);
      }
  
  return false;
}
