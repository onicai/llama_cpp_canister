# Port of llama.cpp to the Internet Computer

THIS IS OUT OF DATE -- WE NOW USE wasi2ic, AND MANY THINGS CAN REMAIN UNCHANGED

## Files required for the llama2 LLM inference engine

First, we determined what files are needed, and listed them in icpp.toml:

Notes: 
 - main_.cpp is the canister equivalent of llama.cpp main.cpp
 - All files: src/llama_cpp_onicai_fork/unicode-data.cpp, src/llama_cpp_onicai_fork/unicode.cpp, src/llama_cpp_onicai_fork/common/json-schema-to-grammar.cpp, src/llama_cpp_onicai_fork/common/build-info.cpp, , src/llama_cpp_onicai_fork/common/grammar-parser.cpp, src/llama_cpp_onicai_fork/common/sampling.cpp, src/llama_cpp_onicai_fork/common/common.cpp, , src/llama_cpp_onicai_fork/llama.cpp, src/*.cpp, src/llama_cpp_onicai_fork/ggml.c, src/llama_cpp_onicai_fork/ggml-alloc.c, src/llama_cpp_onicai_fork/ggml-backend.c, src/llama_cpp_onicai_fork/ggml-quants.c
 

We made sure it worked properly using a native build, and then proceeded to port it to the iC.

## Porting to IC

When porting the llama.cpp application to a Smart Contract running on the IC, the following had to be changed:
2. No exceptions
3. No curl (to dowload a model file)
4. No OS or machine specific capabilities (APPLE vs WIN32 vs ...)
5. No CUDA (GPU acceleration)
6. No threading (Multi Threading acceleration)
7. No Microsoft Visual C++ compiler
8. No main()+console() program -> instead use canister request/response API

Because llama.cpp is designed to be super efficient when running local, by default it does a 
lot of checking against the hardware it is being compiled on. There are no build-in options
to bypass those checks, and the only option is to patch it.


## 1. No file IO
Wherever there was a file-io or directory creation code, we simply outcommented it.

## 2. No exceptions
The IC does not handle exceptions. Our approach is to:
- replace all `throw` statements with an IC_API::trap
- outcomment try - catch
- outcomment `// #include <signal.h>`

For example:
```
# In function: bool gpt_params_parse_ex
# replace:
throw std::invalid_argument("error: unknown argument: " + arg);
# with:
IC_API::trap(std::string("INVALID ARGUMENT: ") + "error: unknown argument: " + arg);

# Then no need for a try-catch, since canister already trapped:
bool gpt_params_parse(int argc, char ** argv, gpt_params & params) {
    bool result = true;
    // try {
        if (!gpt_params_parse_ex(argc, argv, params)) {
            gpt_print_usage(argc, argv, gpt_params());
            exit(0);
        }
    // }
    // catch (const std::invalid_argument & ex) {
    //     fprintf(stderr, "%s\n", ex.what());
    //     gpt_print_usage(argc, argv, gpt_params());
    //     exit(1);
    // }
    return result;
}
```

## 3. No curl (to dowload a model file)
We do not use curl to dowload model files, so outcommented all sections with:
```
#if defined(LLAMA_USE_CURL)
...
#endif
```

## 4. No OS or machine specific capabilities (APPLE vs WIN32 vs ...)
Since we're building the wasm on a Linux/Mac/Windows machine, we need to 
outcomment preprocessor sections like these, else the compiler will throw errors:
```
#if defined(__APPLE__) && defined(__MACH__)
...
#

#ifdef __linux__
...
#endif

#if defined(_WIN32)
...
#endif
```

## 5. No CUDA (GPU acceleration)


## 6. No threading (Multi Threading acceleration)
The llama.cpp application does not provide an option to compile without threading.
This made it quite involved to port, and these are patches applied:

- patch use of `std::thread::hardware_concurrency()`
  ```
  // AB PATCH
  // unsigned int n_threads = std::thread::hardware_concurrency(); 
  unsigned int n_threads = 1;
  ```

- patch and outcomment code that checks cpu details, eg:
  ```
  /**
  * Returns number of CPUs on system that are useful for math.
  */
  int get_math_cpu_count() {
  // #if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__)
  //     int cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  //     if (cpu_count < 1) {
  //         return get_num_physical_cores();
  //     }
  //     if (is_hybrid_cpu()) {
  //         cpu_set_t affinity;
  //         if (!pthread_getaffinity_np(pthread_self(), sizeof(affinity), &affinity)) {
  //             int result = count_math_cpus(cpu_count);
  //             pthread_setaffinity_np(pthread_self(), sizeof(affinity), &affinity);
  //             if (result > 0) {
  //                 return result;
  //             }
  //         }
  //     }
  // #endif
      return get_num_physical_cores();
  }
  ```

## 7. No Microsoft Visual C++ compiler
Nothing needs to be changed, but just mentioning it here that we
use the clang++ compiler, and statements like this will be ignored:
```
#if defined(_MSC_VER)
...
#endif
```

## 8. No main() program -> instead use canister endpoints API