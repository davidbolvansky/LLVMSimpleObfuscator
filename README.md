# LLVMSimpleObfuscator

Ideas:
- inserts bogus control flow near any return statements, implementation details in LLVMSimpleObfuscator.cpp with simple schema which shows how IR is transformed.
- using opaque predicate from literature x(x + 1) == 0 mod 2 which is true for all possible x. Source: https://d-nb.info/1204236666/34 , I also created formal proof in Alive2 tool: https://alive2.llvm.org/ce/z/0ktpG0
- in fake computing block as a fake value to be returned is 'poison' for simplicity (otherwise some nontrivial computation should be used), as it can be constructed for any type.

Build pass against installed LLVM (I have a fresh build of LLVM master):
mkdir build
cd build
make

Simple program in factorial is in factorial.c. 

clang -O1 factorial.c 
./a.out
echo $?
120 // factorial(5), OK

Get LLVM IR:
clang -O1 factorial.c  -S -emit-llvm

Run pass with:
opt factorial.ll -load-pass-plugin=./build/libLLVMSimpleObfuscatorPass.so --passes=obfuscator -S -o new_factorial.ll

Create new binary:
llc new_factorial.ll
clang new_factorial.ll

Verify:
./a.out
echo $?
120 // factorial(5), OK