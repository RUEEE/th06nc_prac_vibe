#include "game_addresses.h"

std::byte* GameModuleBase() noexcept
{
    return reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
}

size_t GameModuleImageSize() noexcept
{
    const std::byte* base = GameModuleBase();
    if (!base)
        return 0;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool IsGameAddressRangeValid(GameAddress address, size_t size) noexcept
{
    const size_t imageSize = GameModuleImageSize();
    const uintptr_t rva = GameRva(address);
    return imageSize != 0 && rva <= imageSize && size <= imageSize - rva;
}
