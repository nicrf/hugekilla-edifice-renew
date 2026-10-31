// Shared by the libFuzzer harnesses of tests/fuzz (roadmap M6). Each harness
// feeds one parser of network or file input with arbitrary bytes; a crash, an
// AddressSanitizer/UBSan report or a failed FUZZ_CHECK is a bug. Built with
// -DEDIFICE_FUZZ=ON (see BUILD.md); without libFuzzer, fuzz_main.cpp runs the
// harness over files or folders (the seed corpora in tests/fuzz/corpus).
#ifndef FUZZH
#define FUZZH

#include "StdAfx.h"   //the game's types (DWORD, Vector, LandDefs, ServerVars...)
#include "multiplay.h"
#include "multiplaystruct.h"
#include "netmsg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CHECK(c) do { if(!(c)) { fprintf(stderr,"%s:%d: fuzz check failed: %s\n",__FILE__,__LINE__,#c); abort(); } } while(0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size);

#endif
