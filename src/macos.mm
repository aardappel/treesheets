#import <AppKit/AppKit.h>
#import <objc/runtime.h>

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

void BypassMacMenuForFunctionKeys(bool (*bypass)()) {
  // AppKit offers function keys to the menu bar before the key window, even without CMD.
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                        handler:^NSEvent *(NSEvent *event) {
                                          if (!(event.modifierFlags & NSEventModifierFlagFunction) ||
                                              !event.window || !bypass()) {
                                            return event;
                                          }
                                          [event.window sendEvent:event];
                                          return nil;
                                        }];
}

void ActivateMacApp() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  // Its replacement -activate in macOS 14 may leave the app in the background.
  [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
}

void ShowMacCompletions(WXWidget field) {
  // wxOSX leaves the first suggestion selected, so that Cocoa inserts it, and accepts it on
  // a character that doesn't continue the word, such as "(". Select none, as on the others.
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    auto sel = @selector(control:textView:completions:forPartialWordRange:indexOfSelectedItem:);
    auto method = class_getInstanceMethod([field class], sel);
    auto original = reinterpret_cast<NSArray *(*)(id, SEL, NSControl *, NSTextView *, NSArray *,
                                                  NSRange, NSInteger *)>(
        method_getImplementation(method));
    method_setImplementation(
        method, imp_implementationWithBlock(^NSArray *(id self, NSControl *control,
                                                       NSTextView *textview, NSArray *words,
                                                       NSRange range, NSInteger *index) {
          auto res = original(self, sel, control, textview, words, range, index);
          if (index) *index = -1;
          return res;
        }));
  });
  [[static_cast<NSTextField *>(field) currentEditor] complete:nil];
}
