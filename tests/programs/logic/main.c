int calls = 0;

func bump() int {
    calls = calls + 1;
    return 1;
}

func main() int {
    if (0 && bump()) {}
    if (1 || bump()) {}
    if (1 && bump()) {}
    if (0 || bump()) {}
    return calls;
}
