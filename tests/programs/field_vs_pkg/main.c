import "math";

struct Pair {
    int n;
};

func main() int {
    Pair p;
    p.n = 40;
    return math.add(p.n, 2);
}
