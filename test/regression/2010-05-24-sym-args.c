// RUN: %llvmgcc -emit-llvm -c -o %t.bc %s
// RUN: %klee --posix-runtime %t.bc --sym-args 0 1 3

int main(int argc, char** argv)
{
  const char* progname = argv[0];
  return 0;
}
