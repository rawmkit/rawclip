#include "version.h"

#include <assert.h>
#include <string.h>

int
main(int argc, char **argv)
{
    if (argc != 2) return 2;
    assert(RAWCLIP_SYSCONFDIR[0] == '/');
    assert(strcmp(RAWCLIP_SYSCONFDIR, argv[1]) == 0);
    return 0;
}
