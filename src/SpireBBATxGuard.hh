#pragma once

#include <stdint.h>

#include <array>
#include <string>

// Spire BBA TX guard (2026-10-24, spoor-a-hangdump; new-features/roguelike/output/hang-dumps-vergelijking.md 2.2-2.5).
// Three identical MEM1 dumps (10:01, 12:38, 16:22) show the GC client's BBA driver taking its "TX done" interrupt path
// twice for one frame: the second time the callback slot at r13-0x296C is already empty and the driver jumps to address
// 0 (PPCHalt). The driver null-checks its RX callbacks but not this one. The client function SpireBBATxGuard
// (system/client-functions/SpireBBATxGuardGC.s) rewrites the 8 words at SPIRE_BBA_TX_GUARD_ADDRESS so an empty slot is
// ignored. These constants are the contract between the .s file, the server gate and the static tests; the function
// only writes when the code still holds the original words, and the server only sends it to the one version whose
// addresses these are (GC EU v1.0, GPOP8P, specific version 3OP0). The config key SpireBBATxGuardPatch (default true)
// turns the sending off.

constexpr uint32_t SPIRE_BBA_TX_GUARD_ADDRESS = 0x80429A78;
constexpr uint32_t SPIRE_BBA_TX_GUARD_SPECIFIC_VERSION = 0x334F5030; // "3OP0"
constexpr const char* SPIRE_BBA_TX_GUARD_FUNCTION_NAME = "SpireBBATxGuard";

// The original words at the address (read from the dumps) and the replacement (encoding verified with capstone 5.0.7):
//   lwz r12,-0x296c(r13); mullw r12,r12,r29; cmplwi r12,0; beq +0x14 (epilogue); subf r0,r29,r29; stw r0,-0x296c(r13);
//   mtlr r12; blrl
constexpr std::array<uint32_t, 8> SPIRE_BBA_TX_GUARD_ORIGINAL = {
    0x2C1D0000, 0x4182001C, 0x818DD694, 0x38000000, 0x387C0000, 0x900DD694, 0x7D8803A6, 0x4E800021};
constexpr std::array<uint32_t, 8> SPIRE_BBA_TX_GUARD_PATCHED = {
    0x818DD694, 0x7D8CE9D6, 0x280C0000, 0x41820014, 0x7C1DE850, 0x900DD694, 0x7D8803A6, 0x4E800021};

// The server sends the function to this version only; every other client gets nothing.
constexpr bool spire_bba_tx_guard_applies(uint32_t specific_version) {
  return specific_version == SPIRE_BBA_TX_GUARD_SPECIFIC_VERSION;
}

// What the client answered in its B3 (r3 of the function).
enum class SpireBBATxGuardResult : uint32_t {
  NO_MATCH = 0, // the code at the address matched neither the original nor the patch; nothing was written
  WRITTEN = 1,
  ALREADY_PRESENT = 2,
};

inline std::string spire_bba_tx_guard_describe(uint32_t return_value) {
  switch (static_cast<SpireBBATxGuardResult>(return_value)) {
    case SpireBBATxGuardResult::NO_MATCH:
      return "code at the address matched neither the original nor the patch; nothing written";
    case SpireBBATxGuardResult::WRITTEN:
      return "patch written";
    case SpireBBATxGuardResult::ALREADY_PRESENT:
      return "patch was already present";
    default:
      return "unexpected return value";
  }
}
