#include "Common.h"

namespace Testing
{

TEST_CLASS( Modules )
{
public:
    TEST_METHOD_INITIALIZE( ClassInitialize )
    {
        AssertEx::NtSuccess( _proc.Attach( GetCurrentProcessId() ) );
    }

    TEST_METHOD( MainModule )
    {
        auto mod = _proc.modules().GetMainModule();
        ValidateModule( *mod, (module_t)GetModuleHandleW( nullptr ) );
    }

    TEST_METHOD( EnumModules )
    {
        auto name = L"kernel32.dll";

        auto manuals = _proc.modules().GetManualModules();
        AssertEx::IsTrue( manuals.empty() );

        auto byLdrList = _proc.modules().GetModule( name, LdrList );

        _proc.modules().reset();
        auto byHeaders = _proc.modules().GetModule( name, Sections );

        ValidateModule( *byLdrList, reinterpret_cast<module_t>(GetModuleHandleW( name )) );
        ValidateModule( *byHeaders, reinterpret_cast<module_t>(GetModuleHandleW( name )) );

        AssertEx::AreEqual( byLdrList->baseAddress, byHeaders->baseAddress );
        AssertEx::AreEqual( byLdrList->name, byHeaders->name );
    }

    void ValidateModule( const ModuleData& mod, module_t expectedBase )
    {
        AssertEx::AreEqual( expectedBase, mod.baseAddress );
        AssertEx::IsNotZero( mod.size );
        AssertEx::IsFalse( mod.manual );
        AssertEx::IsFalse( mod.name.empty() );
        AssertEx::IsFalse( mod.fullPath.empty() );

#ifdef USE64
        AssertEx::AreEqual( static_cast<uint32_t>(mt_mod64), static_cast<uint32_t>(mod.type) );
#else
        AssertEx::AreEqual( static_cast<uint32_t>(mt_mod32), static_cast<uint32_t>(mod.type) );
#endif
    }

    TEST_METHOD( Export )
    {
        auto expected = GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "CreateFileW" );
        AssertEx::IsNotNull( reinterpret_cast<void*>(expected) );

        auto result = _proc.modules().GetExport( L"kernel32.dll", "CreateFileW" );
        AssertEx::IsTrue( result.success() );
        AssertEx::AreEqual( reinterpret_cast<ptr_t>(expected), result->procAddress );
    }

    // Callers pass a GetModule result directly, which is null for a module that is not loaded
    TEST_METHOD( MissingModule )
    {
        auto mod = _proc.modules().GetModule( L"no-such-module.dll" );

        AssertEx::AreEqual( STATUS_INVALID_PARAMETER_1, _proc.modules().GetExport( mod, "CreateFileW" ).status );
        AssertEx::AreEqual( STATUS_INVALID_PARAMETER_1, _proc.modules().Unload( mod ) );
        AssertEx::IsFalse( _proc.modules().Unlink( mod ) );
    }

    TEST_METHOD( InjectUnload )
    {
        InjectUnload( GetTestHelperHost32(), GetTestHelperDll32() );
        InjectUnload( GetTestHelperHost64(), GetTestHelperDll64() );
    }

    void InjectUnload( const std::wstring& hostPath, const std::wstring& dllPath )
    {
        // Suspended with forced loader initialization, so kernel32 is loaded before Inject looks it up
        Process proc;
        AssertEx::NtSuccess( proc.CreateAndAttach( hostPath, true, true ) );

        auto injected = proc.modules().Inject( dllPath );
        AssertEx::NtSuccess( injected.status );
        AssertEx::IsNotNull( injected.result().get() );
        auto name = injected.result()->name;

        // A separate object has no RPC environment from Inject, so Unload must create its own
        // for the new thread it runs LdrUnloadDll in
        Process unloader;
        AssertEx::NtSuccess( unloader.Attach( proc.pid() ) );

        auto mod = unloader.modules().GetModule( name, LdrList );
        AssertEx::IsNotNull( mod.get() );
        AssertEx::AreEqual( STATUS_SUCCESS, unloader.modules().Unload( mod ) );

        unloader.modules().reset();
        AssertEx::IsNull( unloader.modules().GetModule( name, LdrList ).get() );

        proc.Terminate();
    }

private:
    Process _proc;
};
}