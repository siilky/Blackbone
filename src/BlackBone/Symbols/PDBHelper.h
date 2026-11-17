#pragma once
#include "../include/Types.h"
#include <3rd_party/DIA/dia2.h>
#include <Objbase.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>

namespace blackbone
{

class PDBHelper
{
public:
    /// <param name="allowNetwork">Allow downloading PDBs from the symbol server</param>
    explicit PDBHelper( bool allowNetwork );
    ~PDBHelper();

    /// <summary>
    /// Initialize symbols for target file
    /// </summary>
    /// <param name="file">Fully qualified path to target PE file</param>
    /// <returns>Status code; HRESULT_FROM_WIN32( ERROR_MOD_NOT_FOUND ) if DIA cannot be created</returns>
    HRESULT Init( const std::wstring& file );

    /// <summary>
    /// Get symbol RVA
    /// </summary>
    /// <param name="symName">Symbol name</param>
    /// <param name="result">Resulting RVA</param>
    /// <returns>Status code</returns>
    HRESULT GetSymAddress( const std::wstring& symName, uint32_t& result );

private:
    /// <summary>
    /// Initialize DIA
    /// </summary>
    /// <returns>Status code</returns>
    HRESULT CoCreateDiaDataSource();

    /// <summary>
    /// Open the PDB of the file from a local symbol store
    /// </summary>
    /// <param name="file">Fully qualified path to target PE file</param>
    /// <returns>Status code</returns>
    HRESULT LoadFromLocalStore( const std::wstring& file );

    /// <summary>
    /// Build module symbol map
    /// </summary>
    /// <returns>Status code</returns>
    HRESULT PopulateSymbols();

private:
    // DIA interfaces
    Microsoft::WRL::ComPtr<IDiaDataSource> _source;
    Microsoft::WRL::ComPtr<IDiaSession>    _session;
    Microsoft::WRL::ComPtr<IDiaSymbol>     _global;

    bool _allowNetwork;                                     // Symbol server access
    bool _coInitialized = false;                            // CoInitialize succeeded and needs CoUninitialize
    std::wstring _sympath;                                  // Symbol search path with network allowed, empty to let DIA decide
    std::unordered_map<std::wstring, uint32_t> _cache;      // Symbol name <--> RVA map
};

}