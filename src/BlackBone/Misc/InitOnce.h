#pragma once
#include "../Config.h"

namespace blackbone
{
    enum InitOptions
    {
        None = 0,
        LoadSymbols = 1,
        AcquirePriveleges = 2,
        All = LoadSymbols | AcquirePriveleges,
    };
    
    //ENUM_OPS(InitOptions)

    BLACKBONE_API bool InitializeOnce(InitOptions options = All);
}