#include "Common.h"
#include <3rd_party/VersionApi.h>

namespace Testing
{
    TEST_CLASS( Symbols )
    {
    public:
        TEST_METHOD( SymbolsMatch )
        {
            SymbolLoader sl;
            SymbolData fromSymbols, fromPatterns;

            auto [ntdll32, ntdll64] = sl.LoadImages();

            AssertEx::IsNotZero( ntdll32.imageBase() );
            AssertEx::IsNotZero( ntdll64.imageBase() );
       
            AssertEx::NtSuccess( sl.LoadFromPatterns( ntdll32, ntdll64, fromPatterns ) );

            AssertEx::NtSuccess( sl.LoadFromSymbols( ntdll32, ntdll64, fromSymbols ), L"PDB symbol loading failed" );

            if (IsWindows8Point1OrGreater())
            {
                AssertEx::IsNotZero( fromSymbols.LdrpInvertedFunctionTable64, L"LdrpInvertedFunctionTable64 missing from PDB" );
                AssertEx::IsNotZero( fromSymbols.LdrProtectMrdata, L"LdrProtectMrdata missing from PDB" );
                AssertEx::IsNotZero( fromSymbols.RtlInsertInvertedFunctionTable64, L"RtlInsertInvertedFunctionTable64 missing from PDB" );
            }

            AssertEx::IsNotZero( fromSymbols.LdrpHandleTlsData32, L"LdrpHandleTlsData32 missing from PDB" );
            AssertEx::AreEqual( fromSymbols.LdrpHandleTlsData32, fromPatterns.LdrpHandleTlsData32, L"LdrpHandleTlsData32" );
            AssertEx::IsNotZero( fromSymbols.LdrpHandleTlsData64, L"LdrpHandleTlsData64 missing from PDB" );
            AssertEx::AreEqual( fromSymbols.LdrpHandleTlsData64, fromPatterns.LdrpHandleTlsData64, L"LdrpHandleTlsData64" );
            AssertEx::IsNotZero( fromSymbols.LdrpInvertedFunctionTable32, L"LdrpInvertedFunctionTable32 missing from PDB" );
            AssertEx::AreEqual( fromSymbols.LdrpInvertedFunctionTable32, fromPatterns.LdrpInvertedFunctionTable32, L"LdrpInvertedFunctionTable32" );
            AssertEx::AreEqual( fromSymbols.LdrpInvertedFunctionTable64, fromPatterns.LdrpInvertedFunctionTable64, L"LdrpInvertedFunctionTable64" );
            AssertEx::AreEqual( fromSymbols.LdrProtectMrdata, fromPatterns.LdrProtectMrdata, L"LdrProtectMrdata" );
            AssertEx::IsNotZero( fromSymbols.RtlInsertInvertedFunctionTable32, L"RtlInsertInvertedFunctionTable32 missing from PDB" );
            AssertEx::AreEqual( fromSymbols.RtlInsertInvertedFunctionTable32, fromPatterns.RtlInsertInvertedFunctionTable32, L"RtlInsertInvertedFunctionTable32" );
            AssertEx::AreEqual( fromSymbols.RtlInsertInvertedFunctionTable64, fromPatterns.RtlInsertInvertedFunctionTable64, L"RtlInsertInvertedFunctionTable64" );

            AssertEx::IsNotZero( fromSymbols.LdrpReleaseTlsEntry32, L"LdrpReleaseTlsEntry32" );
            AssertEx::IsNotZero( fromSymbols.LdrpReleaseTlsEntry64, L"LdrpReleaseTlsEntry64" );
        }
    };
}
