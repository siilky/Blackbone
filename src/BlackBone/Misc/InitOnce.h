#pragma once
#include "../Config.h"

namespace blackbone
{
enum InitOptions
{
    None = 0,
    AcquirePriveleges = 1,
    All = AcquirePriveleges,
};

/// <summary>
/// Process-wide one-time initialization. Only the first call's options take effect; Process
/// construction and dynamic imports call it with the defaults, so a caller that needs other
/// options must call it before using the library.
/// </summary>
BLACKBONE_API bool InitializeOnce( InitOptions options = All );
}