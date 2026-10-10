struct Box {
    int v;
};

func Box::split(this self Box) (int, int) {
    return self.v, self.v + 1;
}

func add(x int, y int) (int, int) {
    return x + y, x;
}

func main() int {
    func pair(n int) (int, int) {
        return n + 2, n;
    }
    sum, orig := add(20, 22);
    var extra, base = add(1, 1);
    extra, base = add(0, 0);
    Box box;
    box.v = 3;
    var left, right = box.split();
    var z, w = func() (int, int) {
        return 0, 0;
    }();
    p, q := pair(0);
    return sum - orig + 20 + base - extra + left - right + 1 + z + w + p - q - 2;
}
