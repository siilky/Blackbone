// Generates src/BlackBone/Symbols/SymbolCatalogData.cpp: ntdll function fingerprints taken from PDB symbols.
// Usage: SymbolCatalogGen <output.cpp> <path>...   (path: ntdll.dll or a directory searched for ntdll.dll)
#include "BlackBone/Asm/LDasm.h"
#include "BlackBone/PE/PEImage.h"
#include "BlackBone/Symbols/PDBHelper.h"
#include "BlackBone/Symbols/SymbolCatalog.h"

#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace blackbone;
namespace fs = std::filesystem;

namespace
{

constexpr size_t MaxFingerprint = 64;
constexpr size_t Margin = 8;                    // Added to the shortest unique length to survive neighbouring builds
constexpr uint32_t MaxDataWalk = 0x400;         // Bytes of the referencing function searched for the data operand

struct SymbolInfo
{
    CatalogSymbol symbol;
    const char* name;
    bool onX64;
};

const SymbolInfo g_symbols[] =
{
    { CatalogSymbol::LdrpHandleTlsData,             "LdrpHandleTlsData",             true },
    { CatalogSymbol::LdrpInvertedFunctionTable,     "LdrpInvertedFunctionTable",     true },
    { CatalogSymbol::RtlInsertInvertedFunctionTable, "RtlInsertInvertedFunctionTable", true },
    { CatalogSymbol::LdrpReleaseTlsEntry,           "LdrpReleaseTlsEntry",           true },
    { CatalogSymbol::LdrProtectMrdata,              "LdrProtectMrdata",              false },
};

// Functions that address the start of LdrpInvertedFunctionTable, in order of preference;
// before 15063 the first two do not exist and RtlInsertInvertedFunctionTable addresses the table itself
const char* const g_dataReferrers[] = { "RtlpInsertInvertedFunctionTableEntry", "RtlxRemoveInvertedFunctionTable", "RtlInsertInvertedFunctionTable" };

struct Build
{
    pe::PEImage image;
    bool is64 = false;
    uint32_t timeDateStamp = 0;
    uint32_t sizeOfImage = 0;
    uint64_t version = 0;                       // Four file version parts, 16 bits each
    std::string versionText;
    std::string sha256;
    std::string label;                          // For diagnostics
    std::vector<uint8_t> relocated;             // 1 where a base relocation covers the byte
    std::map<CatalogSymbol, uint32_t> rva;      // PDB RVAs of the catalog symbols
    std::vector<uint32_t> dataReferrers;        // PDB RVAs of g_dataReferrers
};

struct Record
{
    CatalogSymbol symbol;
    bool is64;
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;
    uint16_t operandOffset = 0;
    uint16_t operandEnd = 0;
    std::vector<size_t> covered;                // Indexes of the builds this record resolves on its own
};

bool g_failed = false;

void Fail( const Build& build, const char* symbol, const char* message )
{
    fprintf( stderr, "error: %s, %s: %s\n", build.label.c_str(), symbol, message );
    g_failed = true;
}

const char* SymbolName( CatalogSymbol symbol )
{
    for (const auto& info : g_symbols)
    {
        if (info.symbol == symbol)
            return info.name;
    }

    return "?";
}

bool Sha256( const std::vector<uint8_t>& data, std::string& hex )
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    uint8_t digest[32] = { };

    const bool ok = NT_SUCCESS( BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) )
        && NT_SUCCESS( BCryptCreateHash( alg, &hash, nullptr, 0, nullptr, 0, 0 ) )
        && NT_SUCCESS( BCryptHashData( hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0 ) )
        && NT_SUCCESS( BCryptFinishHash( hash, digest, sizeof( digest ), 0 ) );

    if (hash)
        BCryptDestroyHash( hash );
    if (alg)
        BCryptCloseAlgorithmProvider( alg, 0 );

    if (ok)
    {
        hex.clear();
        for (uint8_t byte : digest)
        {
            char text[3];
            snprintf( text, sizeof( text ), "%02x", byte );
            hex += text;
        }
    }

    return ok;
}

// The string table is read instead of VS_FIXEDFILEINFO: without a Windows 10 manifest the fixed version of system files is reported as 6.2
bool ReadFileVersion( const std::wstring& path, uint64_t& version, std::string& text )
{
    const DWORD size = GetFileVersionInfoSizeW( path.c_str(), nullptr );
    std::vector<uint8_t> data( size );

    struct { WORD language, codePage; }* translation = nullptr;
    wchar_t* value = nullptr;
    UINT length = 0;
    if (size == 0
        || !GetFileVersionInfoW( path.c_str(), 0, size, data.data() )
        || !VerQueryValueW( data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&translation), &length ) || !translation)
    {
        return false;
    }

    wchar_t block[64];
    swprintf_s( block, L"\\StringFileInfo\\%04x%04x\\FileVersion", translation->language, translation->codePage );

    // "10.0.26100.9974 (WinBuild.160101.0800)"
    unsigned parts[4] = { };
    if (!VerQueryValueW( data.data(), block, reinterpret_cast<LPVOID*>(&value), &length ) || !value
        || swscanf_s( value, L"%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3] ) != 4)
    {
        return false;
    }

    version = (uint64_t( parts[0] ) << 48) | (uint64_t( parts[1] ) << 32) | (uint64_t( parts[2] ) << 16) | parts[3];

    char buf[64];
    snprintf( buf, sizeof( buf ), "%u.%u.%u.%u", parts[0], parts[1], parts[2], parts[3] );
    text = buf;
    return true;
}

/// <summary>
/// Mark the bytes covered by base relocations: HIGHLOW is 4 bytes, DIR64 is 8
/// </summary>
void ReadRelocations( Build& build )
{
    build.relocated.assign( build.sizeOfImage, 0 );

    auto block = reinterpret_cast<const uint8_t*>(build.image.DirectoryAddress( IMAGE_DIRECTORY_ENTRY_BASERELOC ));
    if (!block)
        return;

    const uint8_t* end = block + build.image.DirectorySize( IMAGE_DIRECTORY_ENTRY_BASERELOC );
    while (block + sizeof( IMAGE_BASE_RELOCATION ) <= end)
    {
        const auto header = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(block);
        if (header->SizeOfBlock < sizeof( IMAGE_BASE_RELOCATION ))
            break;

        const auto entries = reinterpret_cast<const uint16_t*>(header + 1);
        const size_t count = (header->SizeOfBlock - sizeof( IMAGE_BASE_RELOCATION )) / sizeof( uint16_t );
        for (size_t i = 0; i < count; i++)
        {
            const unsigned type = entries[i] >> 12;
            const size_t length = type == IMAGE_REL_BASED_HIGHLOW ? 4 : type == IMAGE_REL_BASED_DIR64 ? 8 : 0;
            for (size_t k = 0; k < length; k++)
            {
                const size_t rva = header->VirtualAddress + (entries[i] & 0xFFF) + k;
                if (rva < build.relocated.size())
                    build.relocated[rva] = 1;
            }
        }

        block += header->SizeOfBlock;
    }
}

std::unique_ptr<Build> LoadBuild( const fs::path& path )
{
    auto build = std::make_unique<Build>();
    build->label = "unreadable build";

    const std::wstring file = fs::absolute( path ).wstring();
    if (const NTSTATUS status = build->image.Load( file, true ); !NT_SUCCESS( status ))
    {
        fprintf( stderr, "error: cannot load %ls: status 0x%08x\n", file.c_str(), status );
        return nullptr;
    }

    std::ifstream stream( file, std::ios::binary );
    std::vector<uint8_t> content( (std::istreambuf_iterator<char>( stream )), std::istreambuf_iterator<char>() );
    if (content.empty() || !Sha256( content, build->sha256 ) || !ReadFileVersion( file, build->version, build->versionText ))
    {
        fprintf( stderr, "error: cannot read the hash or version of %ls\n", file.c_str() );
        return nullptr;
    }

    const auto base = static_cast<const uint8_t*>(build->image.base());
    const auto headers = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    build->is64 = build->image.mType() == mt_mod64;
    build->timeDateStamp = headers->FileHeader.TimeDateStamp;
    build->sizeOfImage = build->image.imageSize();
    ReadRelocations( *build );

    char label[128];
    snprintf( label, sizeof( label ), "%s %s (TimeDateStamp 0x%08X, SizeOfImage 0x%X)", build->is64 ? "x64" : "x86",
              build->versionText.c_str(), build->timeDateStamp, build->sizeOfImage );
    build->label = label;
    return build;
}

bool LoadSymbols( Build& build, const fs::path& path )
{
    PDBHelper pdb( false );
    const HRESULT hr = pdb.Init( fs::absolute( path ).wstring() );
    if (FAILED( hr ))
    {
        fprintf( stderr, "error: %s: PDB not loaded, HRESULT 0x%08x\n", build.label.c_str(), hr );
        return false;
    }

    auto find = [&pdb]( const char* name, uint32_t& rva )
    {
        return SUCCEEDED( pdb.GetSymAddress( std::wstring( name, name + strlen( name ) ), rva ) ) && rva != 0;
    };

    bool ok = true;
    for (const auto& info : g_symbols)
    {
        if (build.is64 && !info.onX64)
            continue;

        uint32_t rva = 0;
        bool found = find( info.name, rva );
        if (!found && info.symbol == CatalogSymbol::LdrpInvertedFunctionTable)
            found = find( "LdrpInvertedFunctionTables", rva );

        if (found)
        {
            build.rva[info.symbol] = rva;
        }
        else
        {
            Fail( build, info.name, "not found in the PDB" );
            ok = false;
        }
    }

    for (const char* name : g_dataReferrers)
    {
        uint32_t rva = 0;
        if (find( name, rva ))
            build.dataReferrers.push_back( rva );
    }

    return ok;
}

/// <summary>
/// Find the first instruction of the function whose 32-bit operand addresses exactly the data symbol
/// </summary>
/// <param name="instruction">RVA of the instruction</param>
/// <param name="operandOffset">Offset of the operand in the instruction</param>
/// <param name="length">Instruction length</param>
/// <returns>true if found</returns>
bool FindDataOperand( const Build& build, uint32_t function, uint32_t dataRva, uint32_t& instruction, uint16_t& operandOffset, uint16_t& length )
{
    for (uint32_t offset = 0; offset < MaxDataWalk;)
    {
        const uint32_t rva = function + offset;
        if (rva + 16 > build.sizeOfImage)
            return false;

        const auto code = reinterpret_cast<uint8_t*>(build.image.ResolveRVAToVA( rva ));
        ldasm_data ld;
        const uint32_t size = ldasm( code, &ld, build.is64 );
        if ((ld.flags & F_INVALID) || size == 0)
            return false;

        if (build.is64)
        {
            int32_t disp = 0;
            if ((ld.flags & F_MODRM) && (ld.flags & F_RELATIVE) && ld.disp_size == 4)
            {
                memcpy( &disp, code + ld.disp_offset, sizeof( disp ) );
                if (rva + size + static_cast<uint32_t>(disp) == dataRva)
                {
                    instruction = rva;
                    operandOffset = ld.disp_offset;
                    length = static_cast<uint16_t>(size);
                    return true;
                }
            }
        }
        else
        {
            // An absolute address is a relocated 4-byte displacement or immediate whose value is based on ImageBase
            const uint8_t candidates[] = { ld.disp_size == 4 ? ld.disp_offset : uint8_t( 0 ), ld.imm_size == 4 ? ld.imm_offset : uint8_t( 0 ) };
            for (uint8_t candidate : candidates)
            {
                uint32_t value = 0;
                if (candidate == 0 || !build.relocated[rva + candidate])
                    continue;

                memcpy( &value, code + candidate, sizeof( value ) );
                if (value - static_cast<uint32_t>(build.image.imageBase()) == dataRva)
                {
                    instruction = rva;
                    operandOffset = candidate;
                    length = static_cast<uint16_t>(size);
                    return true;
                }
            }
        }

        offset += size;
    }

    return false;
}

/// <summary>
/// Read MaxFingerprint bytes at the RVA with the position-dependent ones masked
/// </summary>
/// <param name="valid">Offset of the first invalid instruction, or MaxFingerprint</param>
bool ReadFingerprint( const Build& build, uint32_t start, std::vector<uint8_t>& bytes, std::vector<uint8_t>& mask, size_t& valid )
{
    if (start + MaxFingerprint + 16 > build.sizeOfImage)
        return false;

    const auto code = reinterpret_cast<uint8_t*>(build.image.ResolveRVAToVA( start ));
    bytes.assign( code, code + MaxFingerprint );
    mask.assign( MaxFingerprint, 0xFF );
    for (size_t i = 0; i < MaxFingerprint; i++)
    {
        if (build.relocated[start + i])
            mask[i] = 0;
    }

    auto clear = [&mask]( size_t from, size_t count )
    {
        for (size_t i = from; i < from + count && i < MaxFingerprint; i++)
            mask[i] = 0;
    };

    valid = MaxFingerprint;
    for (size_t offset = 0; offset < MaxFingerprint;)
    {
        ldasm_data ld;
        const uint32_t size = ldasm( code + offset, &ld, build.is64 );
        if ((ld.flags & F_INVALID) || size == 0)
        {
            valid = offset;
            break;
        }

        // RIP-relative displacement; rel8 stays unmasked
        if (build.is64 && (ld.flags & F_MODRM) && (ld.flags & F_RELATIVE) && ld.disp_size == 4)
            clear( offset + ld.disp_offset, 4 );

        // call, jmp and jcc rel32
        if ((ld.flags & F_RELATIVE) && (ld.flags & F_IMM) && !(ld.flags & F_MODRM) && ld.imm_size == 4)
            clear( offset + ld.imm_offset, 4 );

        offset += size;
    }

    for (size_t i = 0; i < MaxFingerprint; i++)
    {
        if (!mask[i])
            bytes[i] = 0;
    }

    return true;
}

bool MakeRecord( const Build& build, const std::vector<std::unique_ptr<Build>>& builds, CatalogSymbol symbol, Record& record )
{
    const char* name = SymbolName( symbol );
    const uint32_t rva = build.rva.at( symbol );

    record.symbol = symbol;
    record.is64 = build.is64;

    uint32_t start = rva;
    if (symbol == CatalogSymbol::LdrpInvertedFunctionTable)
    {
        uint16_t length = 0;
        bool found = false;
        for (uint32_t referrer : build.dataReferrers)
        {
            found = FindDataOperand( build, referrer, rva, start, record.operandOffset, length );
            if (found)
                break;
        }

        if (!found)
        {
            Fail( build, name, "no instruction of the referencing functions addresses the data" );
            return false;
        }

        record.operandEnd = build.is64 ? length : 0;
    }

    size_t valid = 0;
    if (!ReadFingerprint( build, start, record.bytes, record.mask, valid ))
    {
        Fail( build, name, "fingerprint is outside the image" );
        return false;
    }

    // Uniqueness only grows with the length, so the shortest unique prefix is found by bisection
    auto unique = [&]( size_t size )
    {
        const auto match = FindUnique( build.image, record.bytes.data(), record.mask.data(), size );
        return match && *match == start;
    };

    if (valid == 0 || !unique( valid ))
    {
        Fail( build, name, valid < MaxFingerprint ? "invalid instruction before the fingerprint becomes unique"
                                                  : "no unique fingerprint within 64 bytes" );
        return false;
    }

    size_t low = 1, high = valid;
    while (low < high)
    {
        const size_t middle = (low + high) / 2;
        if (unique( middle ))
            high = middle;
        else
            low = middle + 1;
    }

    size_t size = (std::min)( low + Margin, MaxFingerprint );

    // A data fingerprint covers the whole instruction that holds the operand
    if (symbol == CatalogSymbol::LdrpInvertedFunctionTable)
        size = (std::max<size_t>)( size, build.is64 ? record.operandEnd : record.operandOffset + 4 );

    if (size > valid)
    {
        Fail( build, name, "invalid instruction inside the fingerprint" );
        return false;
    }

    // Agreement between entries turns a wrong match into a miss only while another entry also matches, so a record
    // must not resolve any input build to a wrong address on its own: an obsolete epoch can match an unrelated
    // function with the same prologue. Each length is checked, because a prefix that is ambiguous in a build
    // can become unique, and wrong, when it grows.
    auto misresolves = [&]( size_t length )
    {
        const CatalogEntry entry = { symbol, build.is64, static_cast<uint16_t>(length), record.bytes.data(), record.mask.data(),
                                     record.operandOffset, record.operandEnd };

        return std::any_of( builds.begin(), builds.end(), [&]( const auto& other )
        {
            const auto found = other->rva.find( symbol );
            if (other->is64 != build.is64 || found == other->rva.end())
                return false;

            const uint32_t resolved = FindInCatalog( other->image, &entry, 1, symbol );
            return resolved != 0 && resolved != found->second;
        } );
    };

    while (misresolves( size ))
    {
        if (++size > valid)
        {
            Fail( build, name, "no valid fingerprint length avoids resolving another build to a wrong address" );
            return false;
        }
    }

    record.bytes.resize( size );
    record.mask.resize( size );
    return true;
}

CatalogEntry ToEntry( const Record& record )
{
    return { record.symbol, record.is64, static_cast<uint16_t>(record.bytes.size()), record.bytes.data(), record.mask.data(),
             record.operandOffset, record.operandEnd };
}

std::vector<CatalogEntry> ToTable( const std::vector<Record>& records )
{
    std::vector<CatalogEntry> table;
    for (const auto& record : records)
        table.push_back( ToEntry( record ) );

    return table;
}

void AppendHex( std::string& out, const std::vector<uint8_t>& data )
{
    for (size_t i = 0; i < data.size(); i++)
    {
        char text[16];
        snprintf( text, sizeof( text ), "%s0x%02X,", i % 16 == 0 ? "    " : " ", data[i] );
        out += text;
        if (i % 16 == 15 || i + 1 == data.size())
            out += '\n';
    }
}

std::string Render( const std::vector<Record>& records, const std::vector<std::unique_ptr<Build>>& builds )
{
    std::string out = "// Generated by SymbolCatalogGen; do not edit.\n#include \"SymbolCatalog.h\"\n\nnamespace blackbone\n{\n\n";
    char text[512];
    for (size_t i = 0; i < records.size(); i++)
    {
        const auto& record = records[i];
        snprintf( text, sizeof( text ), "// %s, %s, %zu bytes, covers:\n", SymbolName( record.symbol ), record.is64 ? "x64" : "x86", record.bytes.size() );
        out += text;
        for (size_t index : record.covered)
        {
            const Build& build = *builds[index];
            snprintf( text, sizeof( text ), "//   %s sha256 %s TimeDateStamp 0x%08X SizeOfImage 0x%X\n", build.versionText.c_str(),
                      build.sha256.c_str(), build.timeDateStamp, build.sizeOfImage );
            out += text;
        }

        snprintf( text, sizeof( text ), "static const uint8_t g_bytes%zu[] =\n{\n", i );
        out += text;
        AppendHex( out, record.bytes );
        out += "};\n";

        snprintf( text, sizeof( text ), "static const uint8_t g_mask%zu[] =\n{\n", i );
        out += text;
        AppendHex( out, record.mask );
        out += "};\n\n";
    }

    out += "const CatalogEntry g_symbolCatalog[] =\n{\n";
    for (size_t i = 0; i < records.size(); i++)
    {
        const auto& record = records[i];
        snprintf( text, sizeof( text ), "    { CatalogSymbol::%s, %s, %zu, g_bytes%zu, g_mask%zu, %u, %u },\n", SymbolName( record.symbol ),
                  record.is64 ? "true" : "false", record.bytes.size(), i, i, record.operandOffset, record.operandEnd );
        out += text;
    }

    snprintf( text, sizeof( text ), "};\nconst size_t g_symbolCatalogSize = %zu;\n\n}\n", records.size() );
    out += text;
    return out;
}

}

int wmain( int argc, wchar_t* argv[] )
{
    if (argc < 3)
    {
        fprintf( stderr, "usage: SymbolCatalogGen <output.cpp> <ntdll.dll or directory>...\n" );
        return 2;
    }

    std::vector<fs::path> files;
    for (int i = 2; i < argc; i++)
    {
        std::error_code ec;
        if (fs::is_directory( argv[i], ec ))
        {
            for (fs::recursive_directory_iterator it( argv[i], fs::directory_options::skip_permission_denied, ec ), end; !ec && it != end; it.increment( ec ))
            {
                if (_wcsicmp( it->path().filename().c_str(), L"ntdll.dll" ) == 0 && it->is_regular_file( ec ))
                    files.push_back( it->path() );
            }
        }
        else
        {
            files.push_back( argv[i] );
        }

        if (ec)
        {
            fprintf( stderr, "error: cannot read %ls: %s\n", argv[i], ec.message().c_str() );
            return 1;
        }
    }

    // An empty scan must not replace the table with an empty one
    if (files.empty())
    {
        fprintf( stderr, "error: no ntdll.dll found in the given paths\n" );
        return 1;
    }

    std::vector<std::pair<std::unique_ptr<Build>, fs::path>> loaded;
    for (const auto& file : files)
    {
        auto build = LoadBuild( file );
        if (!build)
            return 1;

        loaded.emplace_back( std::move( build ), file );
    }

    // Deterministic order independent of the file locations; of equal builds the file with the smallest hash is kept
    auto key = []( const Build& b ) { return std::make_tuple( b.is64, b.version, b.timeDateStamp, b.sizeOfImage, b.sha256 ); };
    std::sort( loaded.begin(), loaded.end(), [&key]( const auto& a, const auto& b ) { return key( *a.first ) < key( *b.first ); } );
    loaded.erase( std::unique( loaded.begin(), loaded.end(), []( const auto& a, const auto& b )
    {
        return a.first->is64 == b.first->is64 && a.first->timeDateStamp == b.first->timeDateStamp && a.first->sizeOfImage == b.first->sizeOfImage;
    } ), loaded.end() );

    std::vector<std::unique_ptr<Build>> builds;
    for (auto& [build, path] : loaded)
    {
        if (!LoadSymbols( *build, path ))
            g_failed = true;

        builds.push_back( std::move( build ) );
    }

    std::vector<Record> records;
    for (const auto& build : builds)
    {
        for (const auto& info : g_symbols)
        {
            const auto found = build->rva.find( info.symbol );
            if (found == build->rva.end())
                continue;

            const auto table = ToTable( records );
            if (FindInCatalog( build->image, table.data(), table.size(), info.symbol ) == found->second)
                continue;

            Record record;
            if (!MakeRecord( *build, builds, info.symbol, record ))
                continue;

            // The fingerprint must resolve its own build on its own: this checks the operand arithmetic of data records
            const CatalogEntry entry = ToEntry( record );
            if (FindInCatalog( build->image, &entry, 1, info.symbol ) != found->second)
            {
                Fail( *build, info.name, "the new fingerprint does not resolve its own build" );
                continue;
            }

            records.push_back( std::move( record ) );
        }
    }

    if (g_failed)
        return 1;

    // A record covers the builds it resolves alone; the final table is checked below as a whole
    for (auto& record : records)
    {
        const CatalogEntry entry = ToEntry( record );
        for (size_t i = 0; i < builds.size(); i++)
        {
            const auto found = builds[i]->rva.find( record.symbol );
            if (builds[i]->is64 == record.is64 && found != builds[i]->rva.end()
                && FindInCatalog( builds[i]->image, &entry, 1, record.symbol ) == found->second)
            {
                record.covered.push_back( i );
            }
        }
    }

    std::sort( records.begin(), records.end(), []( const Record& a, const Record& b )
    {
        return std::make_tuple( a.symbol, a.is64, a.covered.empty() ? SIZE_MAX : a.covered.front() )
             < std::make_tuple( b.symbol, b.is64, b.covered.empty() ? SIZE_MAX : b.covered.front() );
    } );

    size_t checks = 0;
    const auto table = ToTable( records );
    for (const auto& build : builds)
    {
        for (const auto& [symbol, rva] : build->rva)
        {
            checks++;
            if (FindInCatalog( build->image, table.data(), table.size(), symbol ) != rva)
                Fail( *build, SymbolName( symbol ), "the final table does not resolve the PDB RVA" );
        }
    }

    for (const auto& record : records)
    {
        if (record.covered.empty())
            fprintf( stderr, "error: %s %s: a record covers no build\n", SymbolName( record.symbol ), record.is64 ? "x64" : "x86" );
    }

    if (g_failed || std::any_of( records.begin(), records.end(), []( const Record& r ) { return r.covered.empty(); } ))
        return 1;

    const std::string content = Render( records, builds );
    std::ofstream output( argv[1], std::ios::binary | std::ios::trunc );
    output.write( content.data(), static_cast<std::streamsize>(content.size()) );
    if (!output.good())
    {
        fprintf( stderr, "error: cannot write %ls\n", argv[1] );
        return 1;
    }

    printf( "%zu builds, %zu records; final validation matched the PDB RVA in %zu of %zu checks\n", builds.size(), records.size(), checks, checks );
    return 0;
}
