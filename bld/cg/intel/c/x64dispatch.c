#include "_cgstd.h"
#include "coderep.h"
#include "model.h"
#include "cgswitch.h"
#include "feprotos.h"
#include <string.h>

static bool x64_elf_active = false;

void X64SetActive( bool active ) { x64_elf_active = active; }

bool X64CheckDispatch( void )
{
    /* Check if the output filename ends in .o (ELF convention)
     * vs .obj (OMF convention). Set by -bt=linux64 → TS_DOS + -fo=xxx.o */
    if( !x64_elf_active ) {
        const char *name = FEAuxInfo( NULL, FEINF_OBJECT_FILE_NAME );
        if( name != NULL ) {
            int len = strlen(name);
            if( len > 2 && name[len-2] == '.' && name[len-1] == 'o' &&
                (len < 4 || name[len-3] != 'b') ) {
                /* Filename ends in .o but NOT .bjo — assume ELF target */
                x64_elf_active = true;
            }
        }
    }
    return( x64_elf_active );
}

bool X64IsActive( void ) { return( x64_elf_active ); }
