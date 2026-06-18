#ifndef JMARKOVJUNIOR_IPC_GENERATED_H
#define JMARKOVJUNIOR_IPC_GENERATED_H

#include <string_view>
#include <array>

namespace jmj { namespace ipc {
    //Note: the null-terminator *is* there, but the string_view does not include it
    inline constexpr std::string_view NamedPipe = "\\\\.\\pipe\\jmj_ipc_1";

    constexpr size_t DefaultMaxGridBytes = 2147483648;
    constexpr size_t DefaultMaxClientName = 1024;

    constexpr uint32_t StdoutStartCode = 42;
    constexpr uint32_t StdoutStopCode = 999;
} }

#endif
