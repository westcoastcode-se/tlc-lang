package math;

export struct Point {
    int x;
    int y;
};

func pair(n int) (int, int) {
    return n + 1, n;
}

export func make(n int) (Point, int) {
    Point p;
    p.x = n;
    p.y = 1;
    return p, n;
}
