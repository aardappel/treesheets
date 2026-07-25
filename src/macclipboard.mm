#import <AppKit/AppKit.h>

MacClipboardResult GetImageFromMacClipboard() {
  @autoreleasepool {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];

    // Check for direct image data types first
    NSArray *supportedTypes = @[ NSPasteboardTypePNG, @"public.jpeg", NSPasteboardTypeTIFF ];
    NSString *bestType = [pasteboard availableTypeFromArray:supportedTypes];

    if (bestType) {
      NSData *data = [pasteboard dataForType:bestType];
      if (data && data.length > 0) {
        double scaleFactor = 1.0;
        NSArray *items = [pasteboard readObjectsForClasses:@[ [NSImage class] ] options:@{}];
        if (items.count > 0) {
          NSImage *nsImage = items.firstObject;
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
        }

        wxMemoryInputStream stream([data bytes], [data length]);
        wxImage wxImg;
        if (wxImg.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
          return {wxImg, scaleFactor};
        }
      }
    }

    return {wxNullImage, 1.0};
  }
}