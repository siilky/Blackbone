#include "Common.h"
#include <algorithm>

namespace Testing
{
    TEST_CLASS( PatternScan )
    {
    public:
        TEST_METHOD_INITIALIZE( ClassInitialize )
        {
            // A suspended x64 helper keeps the fixture stable and preserves WOW64 scanning from x86 tests.
            AssertEx::NtSuccess( _proc.CreateAndAttach( GetTestHelperHost64(), true, false ) );

            auto allocation = _proc.memory().Allocate( _bytes.size(), PAGE_READWRITE );
            AssertEx::NtSuccess( allocation.status );
            _fixture = std::move( allocation.result() );
            AssertEx::NtSuccess( _proc.memory().Write( _fixture.ptr(), _bytes.size(), _bytes.data() ) );
        }

        TEST_METHOD_CLEANUP( ClassCleanup )
        {
            _fixture.Reset();
            _proc.Terminate();
        }

        // Scan all allocated process memory
        TEST_METHOD( Simple )
        {
            PatternSearch ps1( _bytes );

            std::vector<ptr_t> results;
            const auto count = ps1.SearchRemoteWhole( _proc, false, 0, results );
            AssertEx::AreEqual( results.size(), count );
            AssertEx::IsTrue( std::find( results.begin(), results.end(), _fixture.ptr() ) != results.end() );
        }

        TEST_METHOD( WithWildcard )
        {
            auto pattern = _bytes;
            pattern[2] = 0xCC;
            PatternSearch ps2( pattern );

            std::vector<ptr_t> results;
            const auto count = ps2.SearchRemote( _proc, 0xCC, _fixture.ptr(), _fixture.size(), results );
            AssertEx::AreEqual( size_t( 1 ), count );
            AssertEx::AreEqual( size_t( 1 ), results.size() );
            AssertEx::AreEqual( _fixture.ptr(), results.front() );

            results.clear();
            AssertEx::IsZero( ps2.SearchRemote( _proc, _fixture.ptr(), _fixture.size(), results ) );
            AssertEx::IsTrue( results.empty() );
        }

    private:
        Process _proc;
        MemBlock _fixture;
        const std::vector<uint8_t> _bytes{ 0x42, 0x6C, 0x61, 0x63, 0x6B, 0x42, 0x6F, 0x6E, 0x65, 0x21 };
    };
}
