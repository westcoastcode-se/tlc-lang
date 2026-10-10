struct Point {
    int x;
    int y;
};

struct Node {
    int n;
    Node *next;
};

func make(a Point) Point {
    Point p;
    p.x = a.x;
    p.y = a.y;
    return p;
}

func Point::sum(this self Point, extra int) int {
    return self.x + self.y + extra;
}

func main() int {
    Point p;
    p.x = 20;
    p.y = 10;
    Point *q;
    q = &p;
    Node head;
    head.n = 4;
    head.next = &head;
    Point made;
    made = make(*q);
    return made.sum(0) + head.next.n + sizeof(Point);
}
