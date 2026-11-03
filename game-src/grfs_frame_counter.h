#pragma once
#if REX_PLATFORM_MAC
#include <dlfcn.h>
#include <cstdint>
#include <cstdlib>
#include <rex/ppc/function.h>

// Forward the original kernel implementation without touching any guest
// register, argument or return value.
extern "C" REX_FUNC(__imp__VdSwap) {
  static auto original = reinterpret_cast<PPCFunc*>(dlsym(RTLD_NEXT, "__imp__VdSwap"));
  using Begin = uint64_t (*)();
  using End = void (*)(uint64_t);
  using Record = void (*)();
  static auto begin = reinterpret_cast<Begin>(dlsym(RTLD_DEFAULT, "GRFSTraceGuestSwapBegin"));
  static auto end = reinterpret_cast<End>(dlsym(RTLD_DEFAULT, "GRFSTraceGuestSwapEnd"));
  static auto record = reinterpret_cast<Record>(dlsym(RTLD_DEFAULT, "GRFSRecordGuestSwap"));
  if (!original) {
    std::fputs("GRFS: original VdSwap implementation unavailable\n", stderr);
    std::abort();
  }
  const bool correlated_trace = begin && end;
  const uint64_t frame = correlated_trace ? begin() : 0;
  original(ctx, base);
  if (correlated_trace) {
    end(frame);
  } else if (record) {
    record();
  }
}
#endif
