// *****************************************************************************
// * macOS port: FTP/SFTP/GDrive are not part of the local-only engine build.    *
// * afs/concrete.cpp still calls these, so provide honest no-ops that fail     *
// * loudly rather than silently pretending remote paths work.                  *
// *****************************************************************************

#include "afs/ftp.h"
#include "afs/sftp.h"

namespace fff
{
void ftpInit()  {} //stub: FTP transport not compiled into this build
void sftpInit() {} //stub: SFTP transport not compiled into this build
}
