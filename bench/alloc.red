// Allocation churn. Most of what this builds dies immediately, so the
// time is dominated by the collector.
class Point {
  init(x, y) {
    this.x = x;
    this.y = y;
  }
  distanceSquared() { return this.x * this.x + this.y * this.y; }
}

let far = 0;
for (let round = 0; round < 4000; round = round + 1) {
  let points = [];
  for (let i = 0; i < 500; i = i + 1) {
    points.push(Point(i % 31, i % 17));
  }
  for (let i = 0; i < points.len(); i = i + 1) {
    if (points[i].distanceSquared() > 500) { far = far + 1; }
  }
}
print(far);
