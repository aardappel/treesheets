#import <AppKit/AppKit.h>
#include <wx/image.h>
#include <wx/mstream.h>

wxImage GetImageFromMacClipboard() {
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *supportedTypes = @[ NSPasteboardTypeTIFF, NSPasteboardTypePNG, @"public.jpeg" ];
    NSString *bestType = [pasteboard availableTypeFromArray:supportedTypes];
    if (!bestType) {
      return wxNullImage;
    }
    NSData *imageData = [pasteboard dataForType:bestType];
    if (!imageData || imageData.length == 0) {
      return wxNullImage;
    }
    wxMemoryInputStream stream([imageData bytes], [imageData length]);
    wxImage wxImg;
    if (wxImg.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
      return wxImg;
    }
    return wxNullImage;
  }
}