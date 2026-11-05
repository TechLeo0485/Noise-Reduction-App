#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vs::voxlock {

struct ConstantTensor {
    std::string name;
    std::vector<std::int64_t> dims;
    const float* data = nullptr; // product(dims) floats
};

// Rewrites a serialized ONNX ModelProto so that the named graph inputs become constant initializers
// holding `tensors`. Everything else is copied byte for byte.
//
// VoxLock's chunk graph recomputes, on every call, 32 per-band key/value projections of the enrollment
// (`context`) and the normalised enrollment spectrum (`enroll_mag`), although both are fixed for a call.
// Once they are constants, ONNX Runtime's constant folding evaluates that branch once at session creation.
// The arithmetic is unchanged (same kernels, same inputs); only where it runs moves.
//
// Works on the protobuf wire format directly (no protobuf dependency). Returns false if the model cannot
// be parsed or an input is missing.
bool bindInputsAsConstants(const std::vector<char>& model, const std::vector<ConstantTensor>& tensors,
                           std::vector<char>& out, std::string& error);

// Sets the integer attribute `attribute` of every node with op type `opType` in the main graph to `value`
// (nodes without that attribute are left alone). `changed` receives the number of nodes rewritten.
bool setNodeAttribute(const std::vector<char>& model, const std::string& opType, const std::string& attribute,
                      std::int64_t value, std::vector<char>& out, int& changed, std::string& error);

} // namespace vs::voxlock
