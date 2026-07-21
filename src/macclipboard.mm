#import <AppKit/AppKit.h>
#include <wx/image.h>
#include <wx/mstream.h>

wxImage GetImageFromMacClipboard() {
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSData *pngData = [pasteboard dataForType:NSPasteboardTypePNG];
    if (!pngData || [pngData length] == 0) {
      return wxNullImage;
    }
    wxMemoryInputStream stream([pngData bytes], [pngData length]);
    wxImage image;
    if (image.LoadFile(stream, wxBITMAP_TYPE_PNG)) {
      return image;
    }
    return wxNullImage;
  }
}