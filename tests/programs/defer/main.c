int acc;

func put(n int) void {
    acc = acc * 3 + n;
}

struct Box {
    int v;
    int w;
};

func Box::put(this self Box, n int) void {
    acc = acc * 3 + self.v + n;
}

func Box::touch(this self Box *) void {
    acc = acc + self.w;
}

func main() int {
    acc = acc + func() int {
        return 0;
    }();
    func inner(n int) void {
        acc = acc * 3 + n;
    }
    {
        defer func() {
        }();
        int arg = 1;
        defer func(v int) {
            acc = acc * 3 + v;
        }(arg);
        arg = 4;
        Box box;
        box.v = 1;
        box.w = 3;
        defer box.put(0);
        defer box.touch();
        box.v = 4;
        box.w = 0;
        defer inner(2);
        if (0) {
            defer put(2);
        }
        for (int k = 0; k < 1; k = k + 1) {
            defer put(1);
            continue;
        }
        int i = 0;
        while (i < 1) {
            defer put(2);
            i = 1;
            break;
        }
    }
    defer put(0);
    return acc;
}
