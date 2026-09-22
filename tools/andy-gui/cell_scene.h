// Terminal-compatible cell frames.
//
// This is kept out of host.h so the native window host can consume pixel
// scenes without pulling terminal cell semantics into its renderer.

#ifndef ANDY_GUI_CELL_SCENE_H
#define ANDY_GUI_CELL_SCENE_H

#include <cstdint>
#include <string>
#include <vector>

namespace andy {

const uint32_t kDefaultColor = 0xff000000u;

struct Cell {
  std::string text;
  uint32_t fg = kDefaultColor;
  uint32_t bg = kDefaultColor;
  uint8_t attr = 0;
  uint8_t width = 1;
};

struct Grid {
  int cols = 0;
  int rows = 0;
  std::vector<Cell> cells;
  int cursor_x = 0;
  int cursor_y = 0;
  bool cursor_on = false;

  void resize(int c, int r) {
    cols = c;
    rows = r;
    cells.assign(static_cast<size_t>(c) * static_cast<size_t>(r), Cell{});
  }

  Cell* at(int x, int y) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return nullptr;
    return &cells[static_cast<size_t>(y) * cols + x];
  }
};

}  // namespace andy

#endif  // ANDY_GUI_CELL_SCENE_H
