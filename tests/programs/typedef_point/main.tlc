struct Point {
    int x;
    int y;
};

typedef Point *Ptr;
typedef int Integer;
typedef int Pair[2];

func make(x int, y int) Point {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

func sum(p Point) int {
    return p.x + p.y;
}

func main() int {
    Point p;
    Ptr q;
    Integer bias;
    Pair extra;
    p = make(40, 2);
    q = &p;
    bias = 0;
    extra[0] = 0;
    extra[1] = 0;
    return sum(*q) + bias + extra[0];
}
