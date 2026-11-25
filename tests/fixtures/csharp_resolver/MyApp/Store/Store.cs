namespace MyApp.Store
{
    public class Store
    {
        public int Count { get; set; }

        public static int Total(int seed)
        {
            return seed + 1;
        }
    }
}
