public class Sig
{
    public static int Arity(int a, int b, int c)
    {
        return a + b + c;
    }

    public static int Typed(long a)
    {
        return 0;
    }

    public static int Defaulted(int a, int b = 2, int c = 3)
    {
        return a + b + c;
    }

    public static int Renamed(int b)
    {
        return b;
    }
}
