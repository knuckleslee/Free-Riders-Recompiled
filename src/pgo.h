#pragma once
// A profile-guided build's training run (scripts/build_pgo.ps1) writes its
// counts when the program exits normally. A benchmark run ends by throwing at
// its present limit, and a shutdown that then hangs is stopped from outside,
// which would lose them: so they are written at the limit too, and again,
// complete, at exit.
#ifdef SFR_PGO_GENERATE
extern "C" int __llvm_profile_write_file(void);
namespace sfr { inline void write_training_profile() { __llvm_profile_write_file(); } }
#else
namespace sfr { inline void write_training_profile() {} }
#endif
