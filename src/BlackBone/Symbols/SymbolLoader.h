#pragma once
#include "SymbolData.h"

namespace blackbone
{

class SymbolLoader
{
public:
    /// <summary>
    /// Loader of ntdll symbols from PDBs
    /// </summary>
    /// <param name="allowNetwork">Allow downloading PDBs from the symbol server</param>
    BLACKBONE_API explicit SymbolLoader( bool allowNetwork );

    /// <summary>
    /// Load symbol RVAs of the native ntdll images
    /// </summary>
    /// <param name="result">Found symbols; a symbol that could not be resolved stays 0</param>
    /// <returns>Loading status</returns>
    BLACKBONE_API NTSTATUS Load( SymbolData& result );

private:
    bool _allowNetwork;     // Symbol server access
    bool _x86OS;            // x86 OS
    bool _wow64Process;     // Current process is wow64 process
};

}
