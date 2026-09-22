// A native pixel scene. Unlike andy.App, this does not use the terminal
// cell canvas at all.
//
//   ./build/red examples/andy_native.red

import "andy" as andy;
import "andy/color" as color;
import "andy/native" as native;

const scene = andy.NativeScene(640, 420)
    .clear(color.rgb(0xf4, 0xf5, 0xf7))
    .text(32, 28, "Settings", color.rgb(0x20, 0x22, 0x26), 24)
    .text(32, 66, "Native pixel layout", color.rgb(0x6b, 0x70, 0x79), 14)
    .input(32, 112, 576, 42, "Ada Lovelace")
    .checkbox(32, 184, true, "Enable notifications")
    .button(440, 250, 168, 44, "Save");

native.Window({"title": "Settings"}).run_scene(scene);
