#pragma once

#include "common_utils.h"

// This file should hold the global option knobs of Deegen
//

// When this option is false, the interpreter won't tier up to the baseline JIT,
// so the VM runs in interpreter-only mode.
//
// One can also soft-disable tiering up at runtime to make the VM run in interpreter-only mode,
// but with this option, the tier-up-related logic in the interpreter are removed altogether,
// so the performance should be better.
//
// TODO: currently the JIT logic is still generated, just unused. We should make this option skip the generation of the JIT tiers altogether.
//
constexpr bool x_allow_interpreter_tier_up_to_baseline_jit = true;

// The interpreter maintains how many bytes of bytecodes in each function it has executed to decide when to tier-up.
// (Note that the metric above is #bytes of bytecodes, not #bytecodes, because it's easier to maintain for the interpreter).
//
// After more than 'multipler * bytecodeLen' bytes of bytecodes have been executed, the function will tier up to baseline JIT.
//
// The current 'multipler' value is chosen based on the following observation (note that the metric below is #bytecodes, not #bytes of bytecodes):
// (1) Our baseline JIT can compile about 19M bytecode/s
// (2) The interpreter can execute 110M ~ 1100M bytecode/s, avg = 447M/s, geomean = 377M/s
//
// Also the number varies largely (for example, doing a string concatenation is clearly way slower than an addition), for now,
// we approximate the rent-to-buy ratio as 20. That is, the cost of JIT'ing 1 bytecode is approximately 20x the cost of interpreting 1 bytecode.
//
// Naturally, we want to pay the one-time cost of JIT'ing the code (T_jit) when the cost we already spent in the interpreter exceeds C * T_jit for some C,
// with a smaller C meaning to JIT more aggressively.
//
// For now we simply choose C = 1, yielding a multiplier of 20.
//
constexpr size_t x_interpreter_tier_up_threshold_bytecode_length_multiplier = 20;
