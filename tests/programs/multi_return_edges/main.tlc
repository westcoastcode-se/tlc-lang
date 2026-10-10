int acc;
int slot;
int global_slot;

func put(n int) void {
    acc = acc * 10 + n;
}

func add(x int, y int) (int, int) {
    return x + y, x;
}

func bump() int {
    slot = slot + 1;
    return slot;
}

func ordered() (int, int) {
    return bump(), bump();
}

func once() (int, int) {
    slot = slot + 1;
    return slot, slot * 10;
}

func observe() (int, int) {
    defer put(1);
    defer put(2);
    return acc, acc + 3;
}

func touch() (int, int) {
    acc = acc + 5;
    return 1, 2;
}

func blank() (int, int) {
    int unused = 1;
}

func id(n int) (int) {
    return n;
}

func wrap() (unsigned int, int) {
    unsigned int n;
    n = 0;
    n = n - 1;
    return n, 1;
}

func big() (long long, int) {
    long long a;
    a = 100000;
    a = a * 100000;
    return a, 2;
}

func narrow() (short, char, bool) {
    return -2, 200, 4;
}

func pick(p int *) (int *, int) {
    return p, 7;
}

func none() (int *, int) {
    return nullptr, 3;
}

struct Point {
    int x;
    int y;
};

func make(n int) (Point, int) {
    Point p;
    p.x = n;
    p.y = n + 1;
    return p, 2;
}

struct Box {
    int v;
    int w;
};

func Box::both(this self Box *) (int, int) {
    return self.v, self.w;
}

static func hidden() (int, int) {
    return 40, 2;
}

func adjust(n int) int {
    int *p = &n;
    n, extra := add(n, 1);
    return *p + extra;
}

func shadow(n int) int {
    int *p = &n;
    {
        n, extra := add(1, 2);
    }
    return *p;
}

func set_outer(n int) int {
    int extra = 0;
    int *p = &n;
    {
        n, extra = add(4, 1);
    }
    return *p + extra;
}

func mix(keep int) int {
    int *p = &keep;
    keep, fresh := add(keep, 2);
    return *p + fresh;
}

func read_global() int {
    return global_slot;
}

func hide(outer int) int {
    func inner() int {
        outer, n := add(1, 2);
        return outer + n;
    }
    return inner() + outer;
}

func main() int {
    int *q;
    int n;
    int *np;
    int nn;
    long long wide;
    int wk;
    short s;
    char ch;
    bool flag;
    Point point;
    int pk;
    Box box;
    int left;
    int right;
    int h1;
    int h2;

    slot = 0;
    a, b := ordered();
    if (a != 1) {
        return 1;
    }
    if (b != 2) {
        return 2;
    }

    slot = 0;
    c, d := once();
    if (c != 1) {
        return 3;
    }
    if (d != 10) {
        return 4;
    }
    if (slot != 1) {
        return 5;
    }

    acc = 0;
    e, f := observe();
    if (e != 0) {
        return 6;
    }
    if (f != 3) {
        return 7;
    }
    if (acc != 21) {
        return 8;
    }

    g, h := blank();
    if (g != 0) {
        return 9;
    }
    if (h != 0) {
        return 10;
    }

    if (id(40) != 40) {
        return 11;
    }
    var single = id(6);
    other := 4;
    if (single + other != 10) {
        return 12;
    }

    u, k := wrap();
    if (u < 1) {
        return 13;
    }
    if (u + 2 + k != 2) {
        return 14;
    }

    wide, wk = big();
    if (wide / 100000 / 100000 + wk != 3) {
        return 15;
    }

    s, ch, flag = narrow();
    if (ch != 200) {
        return 16;
    }
    if (flag != 1) {
        return 17;
    }
    if (s != -2) {
        return 18;
    }

    int x = 5;
    q, n = pick(&x);
    if (q == nullptr) {
        return 19;
    }
    if (*q + n != 12) {
        return 20;
    }

    np, nn = none();
    if (np != nullptr) {
        return 21;
    }
    if (nn != 3) {
        return 22;
    }

    point, pk = make(10);
    if (point.x + point.y + pk != 23) {
        return 23;
    }

    box.v = 8;
    box.w = 9;
    left, right = box.both();
    if (left + right != 17) {
        return 24;
    }

    if (adjust(10) != 21) {
        return 25;
    }
    if (shadow(10) != 10) {
        return 26;
    }
    if (set_outer(10) != 9) {
        return 27;
    }
    if (mix(6) != 14) {
        return 28;
    }

    global_slot = 7;
    global_slot, extra := add(3, 1);
    if (read_global() + extra != 10) {
        return 29;
    }
    extra, single = add(0, 0);
    if (extra != 0) {
        return 30;
    }
    if (single != 0) {
        return 31;
    }

    if (hide(10) != 14) {
        return 32;
    }

    h1, h2 = hidden();
    if (h1 + h2 != 42) {
        return 33;
    }

    acc = 1;
    {
        defer touch();
    }
    if (acc != 6) {
        return 34;
    }
    return 42;
}
