func add4(p int *) int {
    return p[0] + p[1] + p[2] + p[3];
}

func main() int {
    int a[4];
    a[0] = 10;
    a[1] = 20;
    a[2] = 7;
    a[3] = 5;
    int *q = a;
    return add4(a) + q[0] - a[0];
}
