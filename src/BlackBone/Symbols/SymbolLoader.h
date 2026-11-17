#pragma once
#include "SymbolData.h"
#include "../PE/PEImage.h"

namespace blackbone
{

class SymbolLoader
{
public:
    /// <summary>
    /// Loader of ntdll symbols from the fingerprint catalog and PDBs
    /// </summary>
    /// <param name="allowNetwork">Allow downloading PDBs from the symbol server</param>
    BLACKBONE_API explicit SymbolLoader( bool allowNetwork );

    /// <summary>
    /// Load symbol RVAs of the native ntdll images: the catalog first, then PDBs for what it missed
    /// </summary>
    /// <param name="result">Found symbols; a symbol that could not be resolved stays 0</param>
    /// <returns>Loading status: the catalog status if it failed, otherwise the PDB status</returns>
    BLACKBONE_API NTSTATUS Load( SymbolData& result );

    /// <summary>
    /// Fill the zero fields from the fingerprint catalog
    /// </summary>
    /// <param name="result">Found symbols; a catalog miss leaves the field 0</param>
    /// <returns>Status of reading the ntdll images; misses are not errors</returns>
    BLACKBONE_API NTSTATUS LoadCatalog( SymbolData& result );

    /// <summary>
    /// Fill the zero fields from PDBs; the PDB of a bitness whose fields are all set is not opened
    /// </summary>
    /// <param name="result">Found symbols; a symbol missing from the PDB leaves the field 0</param>
    /// <returns>Status of reading the ntdll images, otherwise the first failure of the PDBs opened</returns>
    BLACKBONE_API NTSTATUS LoadPdb( SymbolData& result );

private:
    NTSTATUS LoadImages( SymbolData& result );

private:
    bool _allowNetwork;     // Symbol server access
    bool _x86OS;            // x86 OS
    bool _wow64Process;     // Current process is wow64 process

    // Disk images the RVAs refer to; the 64-bit one stays unloaded on an x86 OS
    pe::PEImage _ntdll32, _ntdll64;
};

}
