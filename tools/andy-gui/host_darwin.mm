// The Cocoa window, for macOS.
//
// A window holding a logical grid drawn into a pixel-sized surface. AppKit
// insists on the main thread, which it has here because andy-gui is a
// process whose main thread does nothing else.
//
// Drawing is by runs rather than by cells: neighbouring cells sharing a
// colour and an attribute are drawn as one string, which is the
// difference between a frame that costs a few dozen draws and one that
// costs several thousand.

#import <Cocoa/Cocoa.h>

#include <vector>

#include "host.h"

namespace {

// The default colours, used for a cell that asked for neither.
const uint32_t kDefaultFg = 0xd8dce4u;
const uint32_t kDefaultBg = 0x16181du;

NSColor* ColorFor(uint32_t value, uint32_t fallback) {
  uint32_t rgb = value;
  if (value == andy::kDefaultColor) rgb = fallback;
  CGFloat r = ((rgb >> 16) & 0xff) / 255.0;
  CGFloat g = ((rgb >> 8) & 0xff) / 255.0;
  CGFloat b = (rgb & 0xff) / 255.0;
  return [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
}

}  // namespace

// The events the view has collected since the last pump, in the protocol
// andy-gui speaks.
@interface AndyView : NSView
@property(nonatomic, assign) andy::Grid* grid;
@property(nonatomic, assign) andy::PixelScene* scene;
@property(nonatomic, assign) CGFloat cellWidth;
@property(nonatomic, assign) CGFloat cellHeight;
@property(nonatomic, assign) CGFloat baseline;
@property(nonatomic, strong) NSFont* font;
@property(nonatomic, strong) NSFont* boldFont;
@property(nonatomic, strong) NSFont* italicFont;
@property(nonatomic, strong) NSMutableArray<NSString*>* events;
@property(nonatomic, assign) BOOL closed;
// Pixel scenes ask for the same few fonts and images every frame.
@property(nonatomic, strong) NSMutableDictionary<NSString*, NSFont*>* sceneFonts;
@property(nonatomic, strong) NSMutableDictionary<NSString*, id>* sceneImages;
@end

@implementation AndyView

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self == nil) return nil;
  _events = [NSMutableArray array];
  _closed = NO;
  _sceneFonts = [NSMutableDictionary dictionary];
  _sceneImages = [NSMutableDictionary dictionary];

  CGFloat size = 13.0;
  NSString* wanted = [[NSProcessInfo processInfo] environment][@"ANDY_FONT_SIZE"];
  if (wanted != nil && [wanted doubleValue] > 4) size = [wanted doubleValue];

  if (@available(macOS 10.15, *)) {
    _font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    _boldFont = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightBold];
  } else {
    _font = [NSFont userFixedPitchFontOfSize:size];
    _boldFont = [NSFont userFixedPitchFontOfSize:size];
  }
  if (_font == nil) _font = [NSFont systemFontOfSize:size];
  if (_boldFont == nil) _boldFont = _font;
  _italicFont = [[NSFontManager sharedFontManager] convertFont:_font
                                                   toHaveTrait:NSItalicFontMask];
  if (_italicFont == nil) _italicFont = _font;

  // These are the native font metrics used to choose the logical grid size.
  // Drawing itself derives a cell's pixel rectangle from the current view,
  // so a native window can be resized by arbitrary pixels without leaving
  // a terminal-like strip along an edge.
  NSDictionary* attributes = @{NSFontAttributeName : _font};
  NSSize advance = [@"M" sizeWithAttributes:attributes];
  _cellWidth = ceil(advance.width);
  _cellHeight = ceil([_font ascender] - [_font descender] + [_font leading]);
  if (_cellHeight < 1) _cellHeight = ceil(size * 1.3);
  _baseline = -[_font descender] + [_font leading] / 2;
  return self;
}

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)add:(NSString*)line {
  [self.events addObject:line];
}

- (NSFont*)sceneFontOfSize:(CGFloat)size attr:(uint8_t)attr {
  const BOOL bold = (attr & andy::kBold) != 0;
  const BOOL mono = (attr & andy::kMono) != 0;
  NSString* key = [NSString stringWithFormat:@"%.2f %d %d", size, bold, mono];
  NSFont* font = self.sceneFonts[key];
  if (font != nil) return font;
  NSFontWeight weight = bold ? NSFontWeightBold : NSFontWeightRegular;
  if (mono) {
    if (@available(macOS 10.15, *)) {
      font = [NSFont monospacedSystemFontOfSize:size weight:weight];
    }
    if (font == nil) font = [NSFont userFixedPitchFontOfSize:size];
  }
  if (font == nil) {
    font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
  }
  self.sceneFonts[key] = font;
  return font;
}

// Decoded once per path. A file that cannot be read is remembered as
// missing too, so a broken image does not cost a disk read every frame.
- (NSImage*)sceneImageAt:(const std::string&)path {
  NSString* key = [NSString stringWithUTF8String:path.c_str()];
  if (key == nil) return nil;
  id cached = self.sceneImages[key];
  if (cached != nil) return cached == [NSNull null] ? nil : cached;
  if ([self.sceneImages count] > 32) [self.sceneImages removeAllObjects];
  NSImage* image = [[NSImage alloc] initWithContentsOfFile:key];
  self.sceneImages[key] = image != nil ? image : (id)[NSNull null];
  return image;
}

- (void)drawPixelScene:(andy::PixelScene*)scene inBounds:(NSRect)bounds {
  for (const andy::PixelCommand& command : scene->commands) {
    CGFloat y = bounds.size.height - command.y - command.height;
    switch (command.kind) {
      case andy::kFill: {
        [ColorFor(command.background, kDefaultBg) setFill];
        NSRectFill(NSMakeRect(command.x, y, command.width, command.height));
        break;
      }
      case andy::kRect: {
        NSRect box = NSMakeRect(command.x, y, command.width, command.height);
        NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:box
                                                               xRadius:command.radius
                                                               yRadius:command.radius];
        if (command.background != andy::kDefaultColor) {
          [ColorFor(command.background, kDefaultBg) setFill];
          [path fill];
        }
        if (command.foreground != andy::kDefaultColor) {
          [ColorFor(command.foreground, kDefaultFg) setStroke];
          [path setLineWidth:command.stroke_width];
          [path stroke];
        }
        break;
      }
      case andy::kLine: {
        [ColorFor(command.foreground, kDefaultFg) setStroke];
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path setLineWidth:command.stroke_width];
        [path moveToPoint:NSMakePoint(command.x,
                                      bounds.size.height - command.y)];
        [path lineToPoint:NSMakePoint(command.x2,
                                      bounds.size.height - command.y2)];
        [path stroke];
        break;
      }
      case andy::kText: {
        CGFloat size = command.height > 0 ? command.height : 14.0;
        NSFont* font = [self sceneFontOfSize:size attr:command.attr];
        NSMutableDictionary* attributes = [NSMutableDictionary dictionary];
        attributes[NSFontAttributeName] = font;
        attributes[NSForegroundColorAttributeName] =
            ColorFor(command.foreground, kDefaultFg);
        if ((command.attr & andy::kUnderline) != 0) {
          attributes[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
        }
        if ((command.attr & andy::kStrike) != 0) {
          attributes[NSStrikethroughStyleAttributeName] =
              @(NSUnderlineStyleSingle);
        }
        NSString* value = [NSString stringWithUTF8String:command.text.c_str()];
        if (value == nil) value = @"\uFFFD";
        [value drawAtPoint:NSMakePoint(command.x,
                                       bounds.size.height - command.y - size)
             withAttributes:attributes];
        break;
      }
      case andy::kImage: {
        NSImage* image = [self sceneImageAt:command.text];
        if (image == nil) break;
        NSSize natural = [image size];
        if (natural.width <= 0 || natural.height <= 0) break;
        CGFloat scale = MIN(1.0, MIN(command.width / natural.width,
                                     command.height / natural.height));
        CGFloat w = floor(natural.width * scale);
        CGFloat h = floor(natural.height * scale);
        NSRect box = NSMakeRect(command.x + floor((command.width - w) / 2),
                                y + floor((command.height - h) / 2), w, h);
        [image drawInRect:box
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1.0
           respectFlipped:YES
                    hints:@{NSImageHintInterpolation : @(NSImageInterpolationHigh)}];
        break;
      }
      case andy::kCursor: {
        [[ColorFor(command.foreground, kDefaultFg)
            colorWithAlphaComponent:0.75] setFill];
        NSRectFillUsingOperation(NSMakeRect(command.x, y, command.width,
                                            command.height),
                                 NSCompositingOperationDifference);
        break;
      }
    }
  }
}

// ---- drawing ----

- (void)drawRect:(NSRect)dirty {
  [ColorFor(andy::kDefaultColor, kDefaultBg) setFill];
  NSRectFill(dirty);
  if (self.scene != nullptr) {
    [self drawPixelScene:self.scene inBounds:[self bounds]];
    return;
  }
  if (self.grid == nullptr || self.grid->cols == 0 || self.grid->rows == 0) return;

  NSRect bounds = [self bounds];
  andy::Grid* grid = self.grid;
  const CGFloat cell_width = bounds.size.width / grid->cols;
  const CGFloat cell_height = bounds.size.height / grid->rows;
  if (cell_width <= 0 || cell_height <= 0) return;
  const CGFloat font_height = [_font ascender] - [_font descender] +
                              [_font leading];
  const CGFloat baseline = (cell_height - font_height) / 2.0 -
                           [_font descender] + [_font leading] / 2.0;

  for (int y = 0; y < grid->rows; y++) {
    CGFloat top = bounds.size.height - (y + 1) * cell_height;
    if (top + cell_height < dirty.origin.y) continue;
    if (top > dirty.origin.y + dirty.size.height) continue;

    int x = 0;
    while (x < grid->cols) {
      andy::Cell* first = grid->at(x, y);
      if (first == nullptr) break;
      // Gather the neighbouring cells that look the same.
      int end = x + 1;
      while (end < grid->cols) {
        andy::Cell* next = grid->at(end, y);
        if (next == nullptr) break;
        if (next->fg != first->fg || next->bg != first->bg ||
            next->attr != first->attr) {
          break;
        }
        end++;
      }

      uint32_t fg = first->fg;
      uint32_t bg = first->bg;
      if ((first->attr & andy::kReverse) != 0) {
        uint32_t swap = fg;
        fg = (bg == andy::kDefaultColor) ? kDefaultBg : bg;
        bg = (swap == andy::kDefaultColor) ? kDefaultFg : swap;
      }

      NSRect run = NSMakeRect(x * cell_width, top,
                              (end - x) * cell_width, cell_height);
      if (bg != andy::kDefaultColor || (first->attr & andy::kReverse) != 0) {
        [ColorFor(bg, kDefaultBg) setFill];
        NSRectFill(run);
      }

      bool anything = false;
      for (int i = x; i < end && !anything; i++) {
        andy::Cell* cell = grid->at(i, y);
        if (cell->width == 0) continue;  // the right half of a wide one
        if (!cell->text.empty() && cell->text != " ") anything = true;
      }

      if (anything) {
        NSFont* font = self.font;
        if ((first->attr & andy::kBold) != 0) font = self.boldFont;
        else if ((first->attr & andy::kItalic) != 0) font = self.italicFont;

        NSColor* ink = ColorFor(fg, kDefaultFg);
        if ((first->attr & andy::kDim) != 0) {
          ink = [ink colorWithAlphaComponent:0.55];
        }
        NSMutableDictionary* attributes = [NSMutableDictionary dictionary];
        attributes[NSFontAttributeName] = font;
        attributes[NSForegroundColorAttributeName] = ink;
        if ((first->attr & andy::kUnderline) != 0) {
          attributes[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
        }
        if ((first->attr & andy::kStrike) != 0) {
          attributes[NSStrikethroughStyleAttributeName] =
              @(NSUnderlineStyleSingle);
        }
        // Drawn one cell at a time so that the grid stays a grid: a
        // font's own spacing would let a run of wide characters drift
        // out of its columns.
        int column = x;
        for (int i = x; i < end; i++) {
          andy::Cell* cell = grid->at(i, y);
          if (cell->width == 0) continue;
          if (cell->text.empty() || cell->text == " ") {
            column += (cell->width >= 2) ? 2 : 1;
            continue;
          }
          // Nil for anything that is not valid UTF-8, which is not
          // supposed to happen and must not be a crash if it does: an
          // exception thrown out of drawRect unwinds through AppKit and
          // takes the autorelease pool with it.
          NSString* piece = [NSString stringWithUTF8String:cell->text.c_str()];
          if (piece == nil) piece = @"\uFFFD";
          [piece drawAtPoint:NSMakePoint(column * cell_width,
                                         top + baseline)
              withAttributes:attributes];
          column += (cell->width >= 2) ? 2 : 1;
        }
      }
      x = end;
    }
  }

  if (grid->cursor_on) {
    CGFloat top = bounds.size.height - (grid->cursor_y + 1) * cell_height;
    NSRect caret = NSMakeRect(grid->cursor_x * cell_width, top,
                              cell_width, cell_height);
    [[ColorFor(andy::kDefaultColor, kDefaultFg)
        colorWithAlphaComponent:0.75] setFill];
    NSRectFillUsingOperation(caret, NSCompositingOperationDifference);
  }
}

// ---- the keyboard ----

- (NSString*)nameForEvent:(NSEvent*)event text:(NSString**)typed {
  NSString* characters = [event charactersIgnoringModifiers];
  *typed = @"";
  if ([characters length] == 0) return nil;
  unichar code = [characters characterAtIndex:0];

  switch (code) {
    case NSUpArrowFunctionKey: return @"up";
    case NSDownArrowFunctionKey: return @"down";
    case NSLeftArrowFunctionKey: return @"left";
    case NSRightArrowFunctionKey: return @"right";
    case NSHomeFunctionKey: return @"home";
    case NSEndFunctionKey: return @"end";
    case NSPageUpFunctionKey: return @"pageup";
    case NSPageDownFunctionKey: return @"pagedown";
    case NSDeleteFunctionKey: return @"delete";
    case NSInsertFunctionKey: return @"insert";
    case 0x7f: return @"backspace";
    case 8: return @"backspace";
    case 13: return @"enter";
    case 3: return @"enter";
    case 9: return @"tab";
    case 25: return @"tab";  // shift and tab
    case 27: return @"escape";
    default: break;
  }
  if (code >= NSF1FunctionKey && code <= NSF12FunctionKey) {
    return [NSString stringWithFormat:@"f%d", (int)(code - NSF1FunctionKey + 1)];
  }
  if (code == ' ') {
    *typed = @" ";
    return @"space";
  }
  if (code < 32) return nil;

  // An ordinary key. The name is the character as it would be without
  // any modifier, and the text is what it actually types, which differ
  // once shift or an input method is involved.
  NSString* name = [characters substringWithRange:NSMakeRange(0, 1)];
  NSString* produced = [event characters];
  NSUInteger flags = [event modifierFlags];
  BOOL ctrl = (flags & NSEventModifierFlagControl) != 0;
  BOOL alt = (flags & NSEventModifierFlagOption) != 0;
  BOOL command = (flags & NSEventModifierFlagCommand) != 0;
  if (!ctrl && !alt && !command && [produced length] > 0) {
    *typed = produced;
    name = produced;
  }
  return name;
}

- (void)keyDown:(NSEvent*)event {
  NSString* typed = nil;
  NSString* name = [self nameForEvent:event text:&typed];
  if (name == nil) return;
  NSUInteger flags = [event modifierFlags];
  // Command is reported as control, because a program written for a
  // terminal binds control and there is no terminal here to disagree.
  int ctrl = ((flags & NSEventModifierFlagControl) != 0 ||
              (flags & NSEventModifierFlagCommand) != 0) ? 1 : 0;
  int alt = (flags & NSEventModifierFlagOption) != 0 ? 1 : 0;
  int shift = (flags & NSEventModifierFlagShift) != 0 ? 1 : 0;
  if (ctrl || alt) typed = @"";
  [self add:[NSString stringWithFormat:@"key %@ %d %d %d %@", name, ctrl, alt,
                                       shift, typed]];
}

- (void)flagsChanged:(NSEvent*)event {
  // Nothing: andy has no use for a modifier on its own.
}

// ---- the mouse ----

- (void)reportMouse:(NSEvent*)event action:(NSString*)action
             button:(int)button wheel:(int)wheel {
  NSPoint where = [self convertPoint:[event locationInWindow] fromView:nil];
  NSRect bounds = [self bounds];
  int x = 0;
  int y = 0;
  if (self.scene != nullptr) {
    // Pixel scenes receive the native pointer position directly. The
    // compatibility canvas below still receives logical cell coordinates.
    x = (int)floor(where.x);
    y = (int)floor(bounds.size.height - where.y);
  } else if (self.grid != nullptr && self.grid->cols > 0 && self.grid->rows > 0) {
    CGFloat cell_width = bounds.size.width / self.grid->cols;
    CGFloat cell_height = bounds.size.height / self.grid->rows;
    x = (int)floor(where.x / cell_width);
    y = (int)floor((bounds.size.height - where.y) / cell_height);
    x = MIN(MAX(x, 0), self.grid->cols - 1);
    y = MIN(MAX(y, 0), self.grid->rows - 1);
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  NSUInteger flags = [event modifierFlags];
  int ctrl = (flags & NSEventModifierFlagControl) != 0 ? 1 : 0;
  int alt = (flags & NSEventModifierFlagOption) != 0 ? 1 : 0;
  int shift = (flags & NSEventModifierFlagShift) != 0 ? 1 : 0;
  [self add:[NSString stringWithFormat:@"mouse %@ %d %d %d %d %d %d %d",
                                       action, x, y, button, wheel, ctrl, alt,
                                       shift]];
}

- (void)mouseDown:(NSEvent*)event {
  [self reportMouse:event action:@"press" button:1 wheel:0];
}
- (void)mouseUp:(NSEvent*)event {
  [self reportMouse:event action:@"release" button:1 wheel:0];
}
- (void)rightMouseDown:(NSEvent*)event {
  [self reportMouse:event action:@"press" button:3 wheel:0];
}
- (void)rightMouseUp:(NSEvent*)event {
  [self reportMouse:event action:@"release" button:3 wheel:0];
}
- (void)otherMouseDown:(NSEvent*)event {
  [self reportMouse:event action:@"press" button:2 wheel:0];
}
- (void)otherMouseUp:(NSEvent*)event {
  [self reportMouse:event action:@"release" button:2 wheel:0];
}
- (void)mouseDragged:(NSEvent*)event {
  [self reportMouse:event action:@"drag" button:1 wheel:0];
}
- (void)rightMouseDragged:(NSEvent*)event {
  [self reportMouse:event action:@"drag" button:3 wheel:0];
}
- (void)mouseMoved:(NSEvent*)event {
  [self reportMouse:event action:@"move" button:0 wheel:0];
}

- (void)scrollWheel:(NSEvent*)event {
  CGFloat amount = [event scrollingDeltaY];
  if (amount == 0) return;
  // One report per notch. A trackpad sends many small deltas, so they
  // are accumulated and only whole notches are passed on, which stops a
  // gentle two finger scroll from moving a list a hundred rows.
  static CGFloat carried = 0;
  carried += amount;
  CGFloat step = [event hasPreciseScrollingDeltas] ? 12.0 : 1.0;
  while (carried >= step) {
    carried -= step;
    [self reportMouse:event action:@"wheel" button:0 wheel:-1];
  }
  while (carried <= -step) {
    carried += step;
    [self reportMouse:event action:@"wheel" button:0 wheel:1];
  }
}

@end

@interface AndyDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) AndyView* view;
@end

@implementation AndyDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  // The program decides whether to stop, not the window manager, so the
  // request is passed on and the window stays until andy says otherwise.
  [self.view add:@"close"];
  return NO;
}

- (void)windowDidBecomeKey:(NSNotification*)note {
  [self.view add:@"focus 1"];
}

- (void)windowDidResignKey:(NSNotification*)note {
  [self.view add:@"focus 0"];
}

@end

namespace andy {
namespace {

class CocoaHost : public Host {
 public:
  bool open(const std::string& title, int cols, int rows) override {
    @autoreleasepool {
      [NSApplication sharedApplication];
      // A regular application, so the window appears in front and takes
      // the keyboard. Without this a program started from a shell gets a
      // window nobody can type into.
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

      view_ = [[AndyView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
      grid_.resize(cols, rows);
      view_.grid = &grid_;
      view_.scene = nullptr;

      NSRect content = NSMakeRect(0, 0, cols * view_.cellWidth,
                                  rows * view_.cellHeight);
      NSUInteger mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable |
                        NSWindowStyleMaskResizable;
      window_ = [[NSWindow alloc] initWithContentRect:content
                                            styleMask:mask
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
      [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
      [window_ setContentView:view_];
      [window_ setAcceptsMouseMovedEvents:YES];
      // The logical grid still changes at character-sized thresholds, but
      // the native window itself is pixel-resizable. AndyView stretches
      // the current grid over the exact content bounds between thresholds.
      [window_ setMinSize:NSMakeSize(view_.cellWidth * 20,
                                     view_.cellHeight * 5)];

      delegate_ = [[AndyDelegate alloc] init];
      delegate_.view = view_;
      [window_ setDelegate:delegate_];

      [window_ center];
      [window_ makeKeyAndOrderFront:nil];
      [window_ makeFirstResponder:view_];
      [NSApp activateIgnoringOtherApps:YES];
      [NSApp finishLaunching];
      return true;
    }
  }

  // Asking how big the window is must not change what it is showing:
  // andy sends only what differed from the last frame, so a grid
  // cleared behind its back would leave the window blank until every
  // cell happened to change. present() is the only thing that writes
  // here.
  void present(const Grid& grid) override {
    scene_.commands.clear();
    view_.scene = nullptr;
    grid_ = grid;
    @autoreleasepool {
      [view_ setNeedsDisplay:YES];
    }
  }

  void present(const PixelScene& scene) override {
    scene_ = scene;
    view_.scene = &scene_;
    @autoreleasepool {
      [view_ setNeedsDisplay:YES];
    }
  }

  void set_pixel_size(int width, int height) override {
    if (window_ == nil || width <= 0 || height <= 0) return;
    @autoreleasepool {
      // Asked-for sizes are kept within the screen, less the menu bar,
      // the Dock and the window's own title bar.
      NSSize size = NSMakeSize(width, height);
      NSScreen* screen = [window_ screen] != nil ? [window_ screen] : [NSScreen mainScreen];
      if (screen != nil) {
        NSRect usable = [window_ contentRectForFrameRect:[screen visibleFrame]];
        size.width = MIN(size.width, floor(usable.size.width));
        size.height = MIN(size.height, floor(usable.size.height));
      }
      [window_ setContentSize:size];
      [window_ center];
    }
  }

  void pixel_size(int* width, int* height) override {
    @autoreleasepool {
      NSRect bounds = [view_ bounds];
      *width = (int)floor(bounds.size.width);
      *height = (int)floor(bounds.size.height);
    }
  }

  void set_pixel_min(int width, int height) override {
    if (window_ == nil || width <= 0 || height <= 0) return;
    @autoreleasepool {
      [window_ setContentMinSize:NSMakeSize(width, height)];
    }
  }

  bool pump(std::vector<std::string>* out, int wait_ms) override {
    @autoreleasepool {
      NSDate* until = [NSDate dateWithTimeIntervalSinceNow:wait_ms / 1000.0];
      for (;;) {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:until
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event == nil) break;
        [NSApp sendEvent:event];
        // Everything after the first is taken without waiting, so a
        // burst is handled in one pass.
        until = [NSDate distantPast];
      }
      for (NSString* line in view_.events) {
        out->push_back(std::string([line UTF8String]));
      }
      [view_.events removeAllObjects];
    }
    return true;
  }

  void close() override {
    @autoreleasepool {
      if (window_ != nil) {
        [window_ setDelegate:nil];
        [window_ close];
        window_ = nil;
      }
    }
  }

  void bell() override { NSBeep(); }

  void set_title(const std::string& title) override {
    @autoreleasepool {
      [window_ setTitle:[NSString stringWithUTF8String:title.c_str()]];
    }
  }

  void set_clipboard(const std::string& text) override {
    @autoreleasepool {
      NSPasteboard* board = [NSPasteboard generalPasteboard];
      [board clearContents];
      [board setString:[NSString stringWithUTF8String:text.c_str()]
               forType:NSPasteboardTypeString];
    }
  }

  void size(int* cols, int* rows) override {
    @autoreleasepool {
      NSRect bounds = [view_ bounds];
      int c = (int)floor(bounds.size.width / view_.cellWidth);
      int r = (int)floor(bounds.size.height / view_.cellHeight);
      *cols = c > 1 ? c : 1;
      *rows = r > 1 ? r : 1;
    }
  }

 private:
  NSWindow* window_ = nil;
  AndyView* view_ = nil;
  AndyDelegate* delegate_ = nil;
  Grid grid_;
  PixelScene scene_;
};

}  // namespace

Host* make_host() { return new CocoaHost(); }

const char* host_name() { return "cocoa"; }

}  // namespace andy
