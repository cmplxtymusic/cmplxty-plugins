#include "OrtModel.h"
#include "ort/onnxruntime_c_api.h"
#include <mutex>
#include <map>
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace rw::ort
{
struct Runtime { const OrtApi* api = nullptr; OrtEnv* env = nullptr; OrtMemoryInfo* mem = nullptr; };

#if defined(_WIN32)
static std::wstring widen (const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w ((size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}
#endif

std::string runtimeFileName()
{
#if defined(_WIN32)
    return "cmplxty-rewired-ort.dll";        // renamed onnxruntime.dll, so it can't collide with another plug-in's copy
#elif defined(__APPLE__)
    return "libonnxruntime.1.23.2.dylib";
#else
    return "libonnxruntime.so.1.23.2";
#endif
}

Runtime* loadRuntime (const std::string& path, std::string& err)
{
    static std::mutex m;
    static std::map<std::string, std::unique_ptr<Runtime>> cache;
    std::lock_guard<std::mutex> l (m);
    if (auto it = cache.find (path); it != cache.end()) return it->second.get();
    using GetBase = const OrtApiBase* (ORT_API_CALL*) ();
    GetBase getBase = nullptr;
#if defined(_WIN32)
    HMODULE h = LoadLibraryExW (widen (path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h == nullptr) { err = "can't load " + path + " (error " + std::to_string ((unsigned) GetLastError()) + ")"; return nullptr; }
    getBase = reinterpret_cast<GetBase> (reinterpret_cast<void*> (GetProcAddress (h, "OrtGetApiBase")));
#else
    void* h = dlopen (path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr) { const char* e = dlerror(); err = "can't load " + path + (e ? std::string (": ") + e : std::string()); return nullptr; }
    getBase = reinterpret_cast<GetBase> (dlsym (h, "OrtGetApiBase"));
#endif
    if (getBase == nullptr) { err = path + " is not ONNX Runtime"; return nullptr; }
    const OrtApi* api = getBase()->GetApi (ORT_API_VERSION);
    if (api == nullptr) { err = "ONNX Runtime at " + path + " is too old (need API " + std::to_string (ORT_API_VERSION) + ")"; return nullptr; }
    auto rt = std::make_unique<Runtime>();
    rt->api = api;
    if (OrtStatus* s = api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "rewired", &rt->env)) { err = api->GetErrorMessage (s); api->ReleaseStatus (s); return nullptr; }
    api->DisableTelemetryEvents (rt->env);
    if (OrtStatus* s = api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &rt->mem)) { err = api->GetErrorMessage (s); api->ReleaseStatus (s); return nullptr; }
    auto* raw = rt.get();
    cache[path] = std::move (rt);
    return raw;
}

static bool check (const OrtApi* api, OrtStatus* s, std::string& err)
{
    if (s == nullptr) return true;
    err = api->GetErrorMessage (s); api->ReleaseStatus (s); return false;
}

Session::~Session()
{
    if (rt != nullptr)
    {
        if (sess) rt->api->ReleaseSession ((OrtSession*) sess);
        if (opts) rt->api->ReleaseSessionOptions ((OrtSessionOptions*) opts);
    }
}

bool Session::load (Runtime& r, const std::string& modelPath, int threads, std::string& err)
{
    rt = &r; auto* api = r.api;
    OrtSessionOptions* o = nullptr;
    if (! check (api, api->CreateSessionOptions (&o), err)) return false;
    opts = o;
    api->SetIntraOpNumThreads (o, threads);
    api->SetInterOpNumThreads (o, 1);
    api->SetSessionGraphOptimizationLevel (o, ORT_ENABLE_ALL);
    OrtSession* s = nullptr;
#if defined(_WIN32)
    const std::wstring w = widen (modelPath);
    if (! check (api, api->CreateSession (r.env, w.c_str(), o, &s), err)) return false;
#else
    if (! check (api, api->CreateSession (r.env, modelPath.c_str(), o, &s), err)) return false;
#endif
    sess = s;
    return true;
}

bool Session::run (const char* inName, const float* in, const std::vector<int64_t>& shape,
                   const std::vector<const char*>& outNames, std::vector<Tensor>& outs, std::string& err)
{
    if (sess == nullptr) { err = "model not loaded"; return false; }
    auto* api = rt->api;
    size_t n = 1; for (auto d : shape) n *= (size_t) d;
    OrtValue* input = nullptr;
    if (! check (api, api->CreateTensorWithDataAsOrtValue (rt->mem, const_cast<float*> (in), n * sizeof (float), shape.data(), shape.size(),
                                                           ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input), err)) return false;
    std::vector<OrtValue*> ov (outNames.size(), nullptr);
    const bool ok = check (api, api->Run ((OrtSession*) sess, nullptr, &inName, &input, 1, outNames.data(), outNames.size(), ov.data()), err);
    api->ReleaseValue (input);
    if (! ok) return false;
    outs.resize (outNames.size());
    bool good = true;
    for (size_t i = 0; i < ov.size(); ++i)
    {
        OrtTensorTypeAndShapeInfo* info = nullptr;
        float* p = nullptr;
        if (good && check (api, api->GetTensorTypeAndShape (ov[i], &info), err))
        {
            size_t nd = 0; api->GetDimensionsCount (info, &nd);
            outs[i].shape.assign (nd, 0); api->GetDimensions (info, outs[i].shape.data(), nd);
            size_t cnt = 0; api->GetTensorShapeElementCount (info, &cnt);
            api->ReleaseTensorTypeAndShapeInfo (info);
            if (check (api, api->GetTensorMutableData (ov[i], (void**) &p), err)) outs[i].data.assign (p, p + cnt);
            else good = false;
        }
        else good = false;
        api->ReleaseValue (ov[i]);
    }
    return good;
}
} // namespace rw::ort
