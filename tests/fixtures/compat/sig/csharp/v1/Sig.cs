public class Sig
{
    public static int Arity(int a, int b)
    {
        return a + b;
    }

    public static int Typed(int a)
    {
        return a;
    }

    public static int Defaulted(int a, int b = 2)
    {
        return a + b;
    }

    public static int Renamed(int a)
    {
        return a;
    }
}
