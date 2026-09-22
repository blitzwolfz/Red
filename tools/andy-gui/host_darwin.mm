// The Cocoa window, for macOS.
//
// A window holding a grid of cells drawn in a fixed pitch font. AppKit
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
@property(nonatomic, assign) CGFloat cellWidth;
@property(nonatomic, assign) CGFloat cellHeight;
@property(nonatomic, assign) CGFloat baseline;
@property(nonatomic, strong) NSFont* font;
@property(nonatomic, strong) NSFont* boldFont;
@property(nonatomic, strong) NSFont* italicFont;
@property(nonatomic, strong) NSMutableArray<NSString*>* events;
@property(nonatomic, assign) BOOL closed;
@end

@implementation AndyView

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self == nil) return nil;
  _events = [NSMutableArray array];
  _closed = NO;

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

  // The cell is the advance of a character in a fixed pitch font, and
  // the line height the font asks for. Rounding the width up to a whole
  // point keeps columns from drifting apart across a wide window.
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

// ---- drawing ----

- (void)drawRect:(NSRect)dirty {
  [ColorFor(andy::kDefaultColor, kDefaultBg) setFill];
  NSRectFill(dirty);
  if (self.grid == nullptr || self.grid->cols == 0) return;

  NSRect bounds = [self bounds];
  andy::Grid* grid = self.grid;

  for (int y = 0; y < grid->rows; y++) {
    CGFloat top = bounds.size.height - (y + 1) * self.cellHeight;
    if (top + self.cellHeight < dirty.origin.y) continue;
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

      NSRect run = NSMakeRect(x * self.cellWidth, top,
                              (end - x) * self.cellWidth, self.cellHeight);
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
          [piece drawAtPoint:NSMakePoint(column * self.cellWidth,
                                         top + self.baseline)
              withAttributes:attributes];
          column += (cell->width >= 2) ? 2 : 1;
        }
      }
      x = end;
    }
  }

  if (grid->cursor_on) {
    CGFloat top = bounds.size.height - (grid->cursor_y + 1) * self.cellHeight;
    NSRect caret = NSMakeRect(grid->cursor_x * self.cellWidth, top,
                              self.cellWidth, self.cellHeight);
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
  int x = (int)floor(where.x / self.cellWidth);
  int y = (int)floor((bounds.size.height - where.y) / self.cellHeight);
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
      // Resizing in whole cells, so the grid never has a half column of
      // window left over along one edge.
      [window_ setContentResizeIncrements:NSMakeSize(view_.cellWidth,
                                                     view_.cellHeight)];
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
    grid_ = grid;
    @autoreleasepool {
      [view_ setNeedsDisplay:YES];
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
};

}  // namespace

Host* make_host() { return new CocoaHost(); }

const char* host_name() { return "cocoa"; }

}  // namespace andy
