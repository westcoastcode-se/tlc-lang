func main() int {
    bool a;
    bool b;
    int n;
    a = true;
    b = false;
    n = 0;
    if (a && !b) {
        n = 40;
    }
    if (b) {
        return 0;
    }
    b = 8;
    if (b != 1) {
        return 0;
    }
    a = false;
    if (a != 0) {
        return 0;
    }
    if (true == false) {
        return 0;
    }
    return n + true + true;
}
