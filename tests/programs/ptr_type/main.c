func identity(p int *) int {
    return *p;
}

func main() int {
    int n = 1;
    int *p = &n;
    int **pp = &p;
    return identity(pp);
}
