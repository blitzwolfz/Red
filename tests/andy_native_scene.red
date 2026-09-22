// The native pixel scene is independent from the terminal cell canvas.

import "andy" as andy;
import "andy/color" as color;

const scene = andy.NativeScene(320, 200);
scene.clear(color.rgb(245, 246, 248))
      .text(24, 20, "Settings", color.rgb(32, 34, 38), 22)
      .input(24, 64, 272, 38, "Ada")
      .button(192, 126, 104, 40, "Save");

print(scene.lines()[0]); // expect: pixel 320 200
print(scene.lines()[1]); // expect: pfill 0 0 320 200 16119544
print(scene.lines()[2]); // expect: ptext 24 20 22 2105894 0 Settings
print(scene.lines()[scene.lines().len() - 1]); // expect: pixel_end
