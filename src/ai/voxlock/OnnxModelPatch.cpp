#include "ai/voxlock/OnnxModelPatch.h"

#include <algorithm>
#include <cstring>

namespace vs::voxlock {

namespace {

// onnx.proto field numbers.
constexpr std::uint32_t kModelGraph = 7;        // ModelProto.graph
constexpr std::uint32_t kGraphNode = 1;         // GraphProto.node
constexpr std::uint32_t kGraphInitializer = 5;  // GraphProto.initializer
constexpr std::uint32_t kGraphInput = 11;       // GraphProto.input
constexpr std::uint32_t kValueInfoName = 1;     // ValueInfoProto.name
constexpr std::uint32_t kTensorDims = 1;        // TensorProto.dims
constexpr std::uint32_t kTensorDataType = 2;    // TensorProto.data_type
constexpr std::uint32_t kTensorName = 8;        // TensorProto.name
constexpr std::uint32_t kTensorRawData = 9;     // TensorProto.raw_data
constexpr std::uint64_t kFloat = 1;             // TensorProto.DataType.FLOAT
constexpr std::uint32_t kNodeOpType = 4;        // NodeProto.op_type
constexpr std::uint32_t kNodeAttribute = 5;     // NodeProto.attribute
constexpr std::uint32_t kAttributeName = 1;     // AttributeProto.name
constexpr std::uint32_t kAttributeInt = 3;      // AttributeProto.i

enum WireType : std::uint32_t { kVarint = 0, kFixed64 = 1, kLengthDelimited = 2, kFixed32 = 5 };

struct Field {
    std::uint32_t number = 0;
    std::uint32_t wireType = 0;
    std::size_t begin = 0;        // first byte of the tag
    std::size_t payloadBegin = 0; // first byte of the payload (after the length for length-delimited)
    std::size_t end = 0;          // one past the last byte
};

class Reader {
public:
    Reader(const char* data, std::size_t size) : data_(reinterpret_cast<const unsigned char*>(data)), size_(size) {}

    [[nodiscard]] bool atEnd() const { return pos_ >= size_; }

    bool varint(std::uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (pos_ >= size_) {
                return false;
            }
            const unsigned char b = data_[pos_++];
            value |= static_cast<std::uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }

    bool next(Field& f) {
        f.begin = pos_;
        std::uint64_t tag = 0;
        if (!varint(tag)) {
            return false;
        }
        f.number = static_cast<std::uint32_t>(tag >> 3);
        f.wireType = static_cast<std::uint32_t>(tag & 7);
        switch (f.wireType) {
        case kVarint: {
            std::uint64_t ignored = 0;
            f.payloadBegin = pos_;
            if (!varint(ignored)) {
                return false;
            }
            break;
        }
        case kFixed64: f.payloadBegin = pos_; pos_ += 8; break;
        case kFixed32: f.payloadBegin = pos_; pos_ += 4; break;
        case kLengthDelimited: {
            std::uint64_t length = 0;
            if (!varint(length) || length > size_ - pos_) {
                return false;
            }
            f.payloadBegin = pos_;
            pos_ += static_cast<std::size_t>(length);
            break;
        }
        default: return false; // groups are not used by ONNX
        }
        if (pos_ > size_) {
            return false;
        }
        f.end = pos_;
        return true;
    }

private:
    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

void writeVarint(std::vector<char>& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

void writeTag(std::vector<char>& out, std::uint32_t number, std::uint32_t wireType) {
    writeVarint(out, (static_cast<std::uint64_t>(number) << 3) | wireType);
}

void writeBytes(std::vector<char>& out, std::uint32_t number, const char* data, std::size_t size) {
    writeTag(out, number, kLengthDelimited);
    writeVarint(out, size);
    out.insert(out.end(), data, data + size);
}

// The `name` of a ValueInfoProto payload, or empty.
std::string valueInfoName(const char* payload, std::size_t size) {
    Reader r(payload, size);
    Field f;
    while (!r.atEnd() && r.next(f)) {
        if (f.number == kValueInfoName && f.wireType == kLengthDelimited) {
            return {payload + f.payloadBegin, f.end - f.payloadBegin};
        }
    }
    return {};
}

std::vector<char> encodeTensor(const ConstantTensor& t) {
    std::vector<char> out;
    std::size_t count = 1;
    for (std::int64_t d : t.dims) {
        writeTag(out, kTensorDims, kVarint);
        writeVarint(out, static_cast<std::uint64_t>(d));
        count *= static_cast<std::size_t>(d);
    }
    writeTag(out, kTensorDataType, kVarint);
    writeVarint(out, kFloat);
    writeBytes(out, kTensorName, t.name.data(), t.name.size());
    // raw_data is little-endian float32, which is the in-memory layout on x86/x64.
    writeBytes(out, kTensorRawData, reinterpret_cast<const char*>(t.data), count * sizeof(float));
    return out;
}

bool patchGraph(const char* graph, std::size_t size, const std::vector<ConstantTensor>& tensors,
                std::vector<char>& out, std::string& error) {
    std::vector<bool> found(tensors.size(), false);
    Reader r(graph, size);
    Field f;
    while (!r.atEnd()) {
        if (!r.next(f)) {
            error = "malformed GraphProto";
            return false;
        }
        if (f.number == kGraphInput && f.wireType == kLengthDelimited) {
            const std::string name = valueInfoName(graph + f.payloadBegin, f.end - f.payloadBegin);
            const auto it = std::find_if(tensors.begin(), tensors.end(), [&](const ConstantTensor& t) { return t.name == name; });
            if (it != tensors.end()) {
                found[static_cast<std::size_t>(it - tensors.begin())] = true;
                continue; // drop the input; it is re-added below as an initializer
            }
        }
        out.insert(out.end(), graph + f.begin, graph + f.end);
    }
    for (std::size_t i = 0; i < tensors.size(); ++i) {
        if (!found[i]) {
            error = "graph input '" + tensors[i].name + "' not found";
            return false;
        }
        const std::vector<char> tensor = encodeTensor(tensors[i]);
        writeBytes(out, kGraphInitializer, tensor.data(), tensor.size());
    }
    return true;
}

// The string field `number` of a message payload, or empty.
std::string stringField(const char* payload, std::size_t size, std::uint32_t number) {
    Reader r(payload, size);
    Field f;
    while (!r.atEnd() && r.next(f)) {
        if (f.number == number && f.wireType == kLengthDelimited) {
            return {payload + f.payloadBegin, f.end - f.payloadBegin};
        }
    }
    return {};
}

// Copies a NodeProto, replacing the `i` of the attribute named `attribute`. Returns false if it has none.
bool patchNode(const char* node, std::size_t size, const std::string& attribute, std::int64_t value, std::vector<char>& out) {
    bool patched = false;
    Reader r(node, size);
    Field f;
    while (!r.atEnd() && r.next(f)) {
        if (f.number == kNodeAttribute && f.wireType == kLengthDelimited &&
            stringField(node + f.payloadBegin, f.end - f.payloadBegin, kAttributeName) == attribute) {
            std::vector<char> attr;
            Reader a(node + f.payloadBegin, f.end - f.payloadBegin);
            Field af;
            while (!a.atEnd() && a.next(af)) {
                if (af.number != kAttributeInt) {
                    attr.insert(attr.end(), node + f.payloadBegin + af.begin, node + f.payloadBegin + af.end);
                }
            }
            writeTag(attr, kAttributeInt, kVarint);
            writeVarint(attr, static_cast<std::uint64_t>(value));
            writeBytes(out, kNodeAttribute, attr.data(), attr.size());
            patched = true;
            continue;
        }
        out.insert(out.end(), node + f.begin, node + f.end);
    }
    return patched;
}

} // namespace

bool setNodeAttribute(const std::vector<char>& model, const std::string& opType, const std::string& attribute,
                      std::int64_t value, std::vector<char>& out, int& changed, std::string& error) {
    out.clear();
    out.reserve(model.size() + 64);
    changed = 0;
    Reader r(model.data(), model.size());
    Field f;
    bool seenGraph = false;
    while (!r.atEnd()) {
        if (!r.next(f)) {
            error = "malformed ModelProto";
            return false;
        }
        if (f.number != kModelGraph || f.wireType != kLengthDelimited || seenGraph) {
            out.insert(out.end(), model.data() + f.begin, model.data() + f.end);
            continue;
        }
        seenGraph = true;
        const char* graph = model.data() + f.payloadBegin;
        std::vector<char> patchedGraph;
        patchedGraph.reserve(f.end - f.payloadBegin + 64);
        Reader g(graph, f.end - f.payloadBegin);
        Field gf;
        while (!g.atEnd()) {
            if (!g.next(gf)) {
                error = "malformed GraphProto";
                return false;
            }
            const char* node = graph + gf.payloadBegin;
            const std::size_t nodeSize = gf.end - gf.payloadBegin;
            if (gf.number == kGraphNode && gf.wireType == kLengthDelimited && stringField(node, nodeSize, kNodeOpType) == opType) {
                std::vector<char> patchedNode;
                patchedNode.reserve(nodeSize + 8);
                if (patchNode(node, nodeSize, attribute, value, patchedNode)) {
                    writeBytes(patchedGraph, kGraphNode, patchedNode.data(), patchedNode.size());
                    ++changed;
                    continue;
                }
            }
            patchedGraph.insert(patchedGraph.end(), graph + gf.begin, graph + gf.end);
        }
        writeBytes(out, kModelGraph, patchedGraph.data(), patchedGraph.size());
    }
    if (!seenGraph) {
        error = "model has no graph";
        return false;
    }
    return true;
}

bool bindInputsAsConstants(const std::vector<char>& model, const std::vector<ConstantTensor>& tensors,
                           std::vector<char>& out, std::string& error) {
    out.clear();
    std::size_t extra = 0;
    for (const ConstantTensor& t : tensors) {
        std::size_t count = 1;
        for (std::int64_t d : t.dims) {
            if (d <= 0) {
                error = "invalid dimension for '" + t.name + "'";
                return false;
            }
            count *= static_cast<std::size_t>(d);
        }
        extra += count * sizeof(float) + 64;
    }
    out.reserve(model.size() + extra + 64);

    Reader r(model.data(), model.size());
    Field f;
    bool patched = false;
    while (!r.atEnd()) {
        if (!r.next(f)) {
            error = "malformed ModelProto";
            return false;
        }
        if (f.number == kModelGraph && f.wireType == kLengthDelimited && !patched) {
            std::vector<char> graph;
            graph.reserve(f.end - f.payloadBegin + extra);
            if (!patchGraph(model.data() + f.payloadBegin, f.end - f.payloadBegin, tensors, graph, error)) {
                return false;
            }
            writeBytes(out, kModelGraph, graph.data(), graph.size());
            patched = true;
            continue;
        }
        out.insert(out.end(), model.data() + f.begin, model.data() + f.end);
    }
    if (!patched) {
        error = "model has no graph";
        return false;
    }
    return true;
}

} // namespace vs::voxlock
