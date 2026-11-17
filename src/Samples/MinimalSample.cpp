#include <BlackBone/Process/Process.h>

using namespace blackbone;

// The smallest practical consumer: attach to a process and read its memory.
// Its linked size is the fixed cost that every user of the library pays.
static const uint32_t g_marker = 0x12345678;

int main( int /*argc*/, char* /*argv[]*/ )
{
    Process self;
    if (!NT_SUCCESS( self.Attach( GetCurrentProcessId() ) ))
        return 1;

    auto value = self.memory().Read<uint32_t>( reinterpret_cast<ptr_t>( &g_marker ) );
    return value && *value == g_marker ? 0 : 2;
}
