#import <AppKit/AppKit.h>

MacClipboardResult GetImageFromMacClipboard() {
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSArray *classes = @[ [NSImage class] ];
    NSDictionary *options = @{};

    if (![pasteboard canReadObjectForClasses:classes options:options]) {
      return {wxNullImage, 1.0};
    }

    NSArray *copiedItems = [pasteboard readObjectsForClasses:classes options:options];
    if (copiedItems.count == 0) {
      return {wxNullImage, 1.0};
    }

    NSImage *nsImage = copiedItems.firstObject;
    if (!nsImage) return {wxNullImage, 1.0};

    double scaleFactor = 1.0;
    NSSize pointSize = [nsImage size];

    for (NSImageRep *rep in [nsImage representations]) {
      if ([rep isKindOfClass:[NSBitmapImageRep class]]) {
        NSInteger pixelWidth = [rep pixelsWide];
        if (pointSize.width > 0 && pixelWidth > 0) {
          scaleFactor = (double)pixelWidth / pointSize.width;
        }
        break;
      }
    }

    NSData *tiffData = [nsImage TIFFRepresentation];
    if (!tiffData) return {wxNullImage, 1.0};

    wxMemoryInputStream stream([tiffData bytes], [tiffData length]);
    wxImage wxImg;
    if (wxImg.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
      return {wxImg, scaleFactor};
    }

    return {wxNullImage, 1.0};
  }
}