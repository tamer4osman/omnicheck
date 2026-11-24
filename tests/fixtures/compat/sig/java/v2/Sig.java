package sig;

public class Sig {
    public static int arity(int a, int b, int c) {
        return a + b + c;
    }

    public static int typed(long a) {
        return 0;
    }

    public static int multi(int a) {
        return a;
    }

    public static int multi(int a, int b) {
        return a + b;
    }

    public static int multi(int a, int b, int c) {
        return a + b + c;
    }
}
