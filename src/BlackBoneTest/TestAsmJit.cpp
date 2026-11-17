#include "Common.h"

namespace Testing
{
    TEST_CLASS( AsmJIT )
    {
    public:
        TEST_METHOD( Add )
        {
            auto asmPtr = AsmFactory::GetAssembler();
            auto& a = *asmPtr;

            a.GenPrologue();
            a->add( a->zcx, a->zdx );
            a->mov( a->zax, a->zcx );
            a.GenEpilogue();

            auto func = reinterpret_cast<intptr_t( __fastcall* )(intptr_t, intptr_t)>(a->make());

            AssertEx::AreEqual( intptr_t( 15 ), func( 10, 5 ) );
            AssertEx::AreEqual( intptr_t( 5 ), func( 10, -5 ) );
        }

        TEST_METHOD( Call )
        {
            auto asmPtr = AsmFactory::GetAssembler();
            auto& a = *asmPtr;

            wchar_t buf[MAX_PATH] = { };

            a.GenPrologue();
            a.GenCall( reinterpret_cast<uintptr_t>(&GetModuleHandle), { ModuleName } );
            a.GenCall( reinterpret_cast<uintptr_t>(&GetModuleFileNameW), { a->zax, buf, _countof( buf ) } );
            a.GenEpilogue();

            auto func = reinterpret_cast<void(*)()>(a->make());
            func();

            std::wstring name = Utils::ToLower( buf );
            AssertEx::AreNotEqual( name.npos, name.rfind( Utils::ToLower( ModuleName ) ) );
        }

#ifdef USE32
        TEST_METHOD( StructCallPreservesNonvolatileRegisters )
        {
            struct Payload
            {
                uint32_t first;
                uint32_t second;
                uint32_t third;
            };

            Payload payload = { 0x12345678, 0x9ABCDEF0, 0x13579BDF };
            Payload received = { };
            uint64_t returnValue = 0;
            uint64_t( __cdecl* callback )(Payload, Payload*) = []( Payload value, Payload* output ) -> uint64_t
            {
                *output = value;
                return 0x123456789ABCDEF0ull;
            };

            auto innerPtr = AsmFactory::GetAssembler();
            auto& inner = *innerPtr;
            std::vector<AsmVariant> args = { payload, &received };
            // Resolve the struct source locally, as RPC does for the target process.
            args.front().new_imm_val = reinterpret_cast<uintptr_t>(args.front().buf.data());

            inner.GenPrologue();
            inner.GenCall( reinterpret_cast<uintptr_t>(callback), args, cc_cdecl );
            inner.GenEpilogue();
            auto innerCode = inner->make();
            AssertEx::IsTrue( innerCode != nullptr );

            auto outerPtr = AsmFactory::GetAssembler();
            auto& outer = *outerPtr;
            // Preserve the test caller independently of the prologue/epilogue under test.
            outer->push( asmjit::host::esi );
            outer->push( asmjit::host::edi );
            outer->mov( asmjit::host::esi, 0x13572468 );
            outer->mov( asmjit::host::edi, 0x24681357 );
            outer.GenCall( innerCode, { } );
            outer->mov( asmjit::host::ecx, reinterpret_cast<uintptr_t>(&returnValue) );
            outer->mov( asmjit::host::dword_ptr( asmjit::host::ecx ), asmjit::host::eax );
            outer->mov( asmjit::host::dword_ptr( asmjit::host::ecx, 4 ), asmjit::host::edx );
            outer->xor_( asmjit::host::esi, 0x13572468 );
            outer->xor_( asmjit::host::edi, 0x24681357 );
            outer->or_( asmjit::host::esi, asmjit::host::edi );
            outer->mov( asmjit::host::eax, asmjit::host::esi );
            outer->pop( asmjit::host::edi );
            outer->pop( asmjit::host::esi );
            outer->ret();

            auto func = reinterpret_cast<uint32_t(*)()>(outer->make());
            AssertEx::IsTrue( func != nullptr );
            auto mismatch = func();

            AssertEx::AreEqual( payload.first, received.first );
            AssertEx::AreEqual( payload.second, received.second );
            AssertEx::AreEqual( payload.third, received.third );
            AssertEx::AreEqual( uint64_t( 0x123456789ABCDEF0ull ), returnValue );
            AssertEx::IsZero( mismatch, L"Struct marshaling must preserve ESI and EDI" );
        }
#endif

        TEST_METHOD( MultiCall )
        {
            auto asmPtr = AsmFactory::GetAssembler();
            auto& a = *asmPtr;

            a.EnableX64CallStack( false );
            auto skip = a->newLabel();

            uint8_t writeBuf[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
            auto filePath = L"DummyFile_MultiCall.dat";
            DWORD bytes = 0;

            AsmStackAllocator stack( a.assembler() );
            ALLOC_STACK_VAR( stack, handle, HANDLE );

            a.GenPrologue();
            a->sub( a->zsp, 0x48 );
            a.GenCall( reinterpret_cast<uintptr_t>(&CreateFileW), { a->zcx, GENERIC_WRITE, 0x7, nullptr, CREATE_ALWAYS, 0, nullptr } );
            a->mov( handle, a->zax );
            a->cmp( a->zax, reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE) );
            a->je( skip );
            a.GenCall( reinterpret_cast<uintptr_t>(&WriteFile), { handle, writeBuf, sizeof( writeBuf ), &bytes, nullptr } );
            a->test( a->zax, a->zax );
            a->jz( skip );
            a.GenCall( reinterpret_cast<uintptr_t>(&CloseHandle), { handle } );
            a->bind( skip );
            a->add( a->zsp, 0x48 );
            a.GenEpilogue();
            
            auto func = reinterpret_cast<BOOL( __fastcall * )(LPCWSTR)>(a->make());
            BOOL b = func( filePath );
            AssertEx::IsTrue( b );

            // Check locally
            auto hFile = Handle( CreateFileW( filePath, GENERIC_READ, 0x7, nullptr, OPEN_EXISTING, FILE_DELETE_ON_CLOSE, nullptr ) );
            AssertEx::IsTrue( hFile );

            uint8_t readBuf[sizeof( writeBuf )] = { };
            b = ReadFile( hFile, readBuf, sizeof( readBuf ), &bytes, nullptr );

            AssertEx::IsTrue( b );
            AssertEx::AreEqual( static_cast<DWORD>(sizeof( readBuf )), bytes );
            AssertEx::IsZero( memcmp( writeBuf, readBuf, sizeof( readBuf ) ) );
        }
    };
}