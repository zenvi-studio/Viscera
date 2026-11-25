#include <cstdio>
#include <expected>
#include <version>

#if !defined(__cpp_lib_expected) || (__cpp_lib_expected < 202202L)
#error std::expected is not available
#endif

int main()
{
    std::expected<int, int> ok{0};
    const std::unexpected<int> unexpected{1};
    std::expected<int, int> failed{unexpected};

    if (!ok || *ok != 0 || failed || failed.error() != 1)
    {
        return 1;
    }

    #ifdef __clang__
    const char *const compiler = "Clang";
    const char *const compilerVersion = __clang_version__;
    #elif defined(__GNUC__)
    const char *const compiler = "GCC";
    const char *const compilerVersion = __VERSION__;
    #elif defined(_MSC_VER)
    const char *const compiler = "MSVC";
    const char *const compilerVersion = "msvc";
    #else
    const char *const compiler = "unknown";
    const char *const compilerVersion = "unknown";
    #endif

    #if defined(_MSVC_STL_VERSION)
    const char *const stdlib = "msvc-stl";
    #elif defined(_LIBCPP_VERSION)
    const char *const stdlib = "libc++";
    #elif defined(__GLIBCXX__)
    const char *const stdlib = "libstdc++";
    #else
    const char *const stdlib = "unknown";
    #endif

    std::printf(
        "compiler=%s %s stdlib=%s __cpp_lib_expected=%lld monadic=not_required\n",
        compiler,
        compilerVersion,
        stdlib,
        static_cast<long long>(__cpp_lib_expected)
    );

    return 0;
}
