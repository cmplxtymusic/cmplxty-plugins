#pragma once
// Minimal ONNX Runtime wrapper. The runtime is loaded at run time (LoadLibrary / dlopen) from a path we choose, so the plug-in never
// depends on the host's DLL search order and needs no import library. Only the C API header (MIT, src/ort/) is compiled in.
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace rw::ort
{
struct Runtime;   // opaque

struct Tensor { std::vector<float> data; std::vector<int64_t> shape; };

class Session
{
public:
    Session() = default;
    ~Session();
    Session (const Session&) = delete;
    Session& operator= (const Session&) = delete;
    bool load (Runtime&, const std::string& modelPath, int threads, std::string& err);
    bool loaded() const noexcept { return sess != nullptr; }
    // one float input -> all outputs (in the order of names); returns false and sets err on failure
    bool run (const char* inName, const float* in, const std::vector<int64_t>& inShape,
              const std::vector<const char*>& outNames, std::vector<Tensor>& outs, std::string& err);
private:
    Runtime* rt = nullptr;
    void* sess = nullptr;    // OrtSession*
    void* opts = nullptr;    // OrtSessionOptions*
};

// loads the shared library once per process; returns nullptr and sets err when it can't
Runtime* loadRuntime (const std::string& libraryPath, std::string& err);
std::string runtimeFileName();   // onnxruntime library file name for this platform
} // namespace rw::ort
