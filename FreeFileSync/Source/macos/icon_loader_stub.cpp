// *****************************************************************************
// * macOS port: replaces base/icon_loader.cpp, which loads GTK icon themes.    *
// * An Offload app needs no per-file icons; NSWorkspace icons can come later.  *
// *****************************************************************************

#include "../base/icon_loader.h"

using namespace zen;

namespace fff
{
FileIconHolder getIconByTemplatePath(const Zstring&, int) { return FileIconHolder(); } //throw SysError
FileIconHolder genericFileIcon        (int)                { return FileIconHolder(); }
FileIconHolder genericDirIcon         (int)                { return FileIconHolder(); }
FileIconHolder getTrashIcon           (int)                { return FileIconHolder(); }
FileIconHolder getFileManagerIcon     (int)                { return FileIconHolder(); }
FileIconHolder getFileIcon            (const Zstring&, int) { return FileIconHolder(); }
ImageHolder    getThumbnailImage      (const Zstring&, int) { return ImageHolder(); }

wxImage extractWxImage(ImageHolder&&)      { return wxImage(); }
wxImage extractWxImage(FileIconHolder&&)   { return wxImage(); }
}
